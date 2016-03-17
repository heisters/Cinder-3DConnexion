#include "3DConnexion.h"

#include <exception>
#include <WinUser.h>
#include <tchar.h>
#include <condition_variable>
#include <atomic>

#include <spwmacro.h>
#include <siapp.h>
#include <virtualkeys.hpp>

#include "cinder/Log.h"

using namespace std;
using namespace connexion;
using namespace cinder;

// Hidden window manager class ------------------------------------------------

typedef std::unique_ptr< class MessageWindowManager > MessageWindowManagerRef;

class MessageWindowManager {
public:
	static MessageWindowManagerRef&	instance();
	static void						destroyInstance();

private:
	static MessageWindowManagerRef	sInstance;

	MessageWindowManager();
public:
	~MessageWindowManager();
private:

	static LRESULT CALLBACK			WndProc( HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam );
	HWND							CreateHiddenWindow( HWND parent );

public:

	void							start( HWND hwnd );
	DeviceRef						initDevice( SiDevID deviceId );
	void							closeDevice( SiDevID deviceId );
	void							closeAllDevices();
	void							setDeviceLED( SiDevID deviceId, bool on = true );
	std::string						getDeviceButtonName( SiDevID deviceId, V3DKey code );

private:
	std::thread						mThread;
	std::mutex						mMutex;
	std::recursive_mutex			mRMutex;
	std::atomic< bool >				mIsReady = false;
	std::condition_variable			mReadyCV;

	HWND							mWindowHndl = NULL;
	Device*							mCurrentDevice = nullptr;

	struct DeviceRegistration {
		DeviceRegistration() {};
		DeviceRegistration( const DeviceRegistration& ) = delete;
		void operator=( DeviceRegistration const & ) = delete;

		DeviceRef		ref;
		SiHdl			handle;
		Device::status	status = Device::status::uninitialized;
		std::string		name;
		std::string		port;
	};
	std::map< SiDevID, DeviceRegistration > mDevices;

	bool							closeRegistration( DeviceRegistration &registration );
};

MessageWindowManagerRef MessageWindowManager::sInstance = nullptr;

MessageWindowManagerRef& MessageWindowManager::instance()
{
	if ( ! sInstance ) {
		sInstance = ( MessageWindowManagerRef )new MessageWindowManager();
	}

	return sInstance;
}

void MessageWindowManager::destroyInstance()
{
}


MessageWindowManager::MessageWindowManager()
{

}

MessageWindowManager::~MessageWindowManager()
{
	closeAllDevices();

	PostMessage( mWindowHndl, WM_QUIT, 0, 0 );
	mThread.join();
}


LRESULT CALLBACK MessageWindowManager::WndProc(	HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam )
{
	if ( instance()->mIsReady ) {

		MSG				msg;
		SiSpwEvent		event;    /* SpaceWare Event */
		SiGetEventData	eventData;/* SpaceWare Event Data */

		msg.hwnd = hwnd;
		msg.message = wm;
		msg.lParam = lParam;
		msg.wParam = wParam;


		/* init Window platform specific data for a call to SiGetEvent */
		SiGetEventWinInit( &eventData, wm, wParam, lParam );

		{
			lock_guard< mutex > lock( instance()->mMutex );

			for ( auto& device : instance()->mDevices )
			{
				/* check whether msg was a 3D mouse event and process it */
				if ( SiGetEvent( device.second.handle, 0, &eventData, &event ) == SI_IS_EVENT )
				{
					device.second.ref->queueEvent( event );
					return 0;
				}
			}
		}

	}

	return DefWindowProc( hwnd, wm, wParam, lParam );
}

void MessageWindowManager::start( HWND parent )
{
	lock_guard< mutex > lock( mMutex );

	if ( mWindowHndl != NULL ) return;


	mThread = thread( bind( &MessageWindowManager::CreateHiddenWindow, this, parent ) );
}

HWND MessageWindowManager::CreateHiddenWindow( HWND parent )
{
	{
		lock_guard< mutex > lock( mMutex );

		WNDCLASSEX wcex;
		wcex.cbSize = sizeof( WNDCLASSEX );
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = GetModuleHandle( NULL );
		wcex.hIcon = NULL;
		wcex.hCursor = LoadCursor( NULL, IDC_ARROW );
		wcex.hbrBackground = (HBRUSH)( COLOR_BTNFACE + 1 );
		wcex.lpszMenuName = NULL;
		wcex.lpszClassName = L"HiddenMessageWindowClass";
		wcex.hIconSm = NULL;
		RegisterClassEx( &wcex );

		/* Create a hidden window owned by our process and parented to the console window */
		mWindowHndl = CreateWindow( wcex.lpszClassName, L"HiddenMessageWindow", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, wcex.hInstance, NULL );

		if ( SiInitialize() == SPW_DLL_LOAD_ERROR )
		{
			throw exception( "Could not load 3DConnexion DLLs" );
		}

		CI_LOG_I( "3DConnexion driver initialized. " << SiGetNumDevices() << " devices attached." );

		mIsReady = true;
	}

	mReadyCV.notify_all();

	MSG msg;
	bool running = true;
	while ( running )
	{
		while ( true )
		{
			{
				lock_guard< mutex > lock( mMutex );
				if ( ! PeekMessage( &msg, mWindowHndl, 0, 0, PM_REMOVE ) ) break;
			}

			if ( msg.message == WM_QUIT )
			{
				running = false;
				break;
			}

			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
	}

	return mWindowHndl;
}

void MessageWindowManager::setDeviceLED( SiDevID deviceId, bool on )
{
	lock_guard< mutex > lock( mMutex );
	
	SiSetLEDs( mDevices.at( deviceId ).handle, on ? 1 : 0 );
}

std::string MessageWindowManager::getDeviceButtonName( SiDevID deviceId, V3DKey code )
{
	std::string s_name;
	{
		lock_guard< mutex > lock( mMutex );

		SiButtonName bt_name;
		SiGetButtonName( mDevices.at( deviceId ).handle, code, &bt_name );
		s_name = bt_name.name;
	}

	return s_name;
}


DeviceRef MessageWindowManager::initDevice( SiDevID deviceId )
{
	if ( ! mIsReady ) mReadyCV.wait( unique_lock< mutex >( mMutex ), [&] { return (bool)mIsReady; } );


	lock_guard< mutex > lock( mMutex );

	mDevices.emplace( piecewise_construct,
		forward_as_tuple( deviceId ),
		forward_as_tuple() );

	SiOpenData			oData;          /* OS Independent data to open ball  */
	DeviceRegistration&	device = mDevices.at( deviceId );

	SiOpenWinInit( &oData, mWindowHndl ); /* init Win. platform specific data  */
	SiSetUiMode( device.handle, SI_UI_NO_CONTROLS ); /* Config SoftButton Win Display */

												/* open data, which will check for device type and return the device handle
												to be used by this function */
	if ( ( device.handle = SiOpen( "Cinder", deviceId, SI_NO_MASK, SI_EVENT, &oData ) ) == NULL )
	{
		SiTerminate();  /* called to shut down the SpaceWare input library */
		device.status = Device::status::error; /* could not open device */
	}
	else
	{
		SiDeviceName devName;
		SiGetDeviceName( device.handle, &devName );
		device.name = devName.name;

		SiDevPort devPort;
		SiGetDevicePort( device.handle, &devPort );
		device.port = devPort.portName;

		device.status = Device::status::ok; /* opened device succesfully */

		// Disallow other applications from using the device
		SpwRetVal result;
		result = SiGrabDevice( device.handle, SPW_FALSE /* soft capture */ );
		if ( result != SPW_NO_ERROR )
		{
			CI_LOG_W( "Could not establish an exclusive claim on " << devName.name );
		}

		// Allow Cinder to manage all button functions
		result = SiSyncSendQuery( device.handle );
		if ( result != SPW_NO_ERROR )
		{
			CI_LOG_W( "Could not reassign buttons to Cinder control" );
		}

		else
		{
			// Since we don't really know how many buttons there are, just set
			// them until we get an error.
			for ( SPWuint32 bt = 0; result == SPW_NO_ERROR && bt < s3dm::MaxKeyCount; ++bt ) {
				SiSyncSetButtonAssignment( device.handle, bt, 0 /* the button event is to be passed straight thru */ );
			}

			if ( result == SI_BAD_HANDLE )
			{
				CI_LOG_W( "Bad handle error reassigning buttons to Cinder control" );
			}
		}
	}

	device.ref = Device::create( deviceId, device.name, device.port, device.status );

	return device.ref;
}

bool MessageWindowManager::closeRegistration( DeviceRegistration & registration )
{
	if ( registration.status == Device::status::ok ) {
		SiClose( registration.handle );
		registration.status = Device::status::uninitialized;
		return true;
	}
	return false;
}

void MessageWindowManager::closeDevice( SiDevID deviceId )
{
	lock_guard< recursive_mutex > lock( mRMutex );

	if ( mDevices.find( deviceId ) != mDevices.end() ) {
		if ( closeRegistration( mDevices.at( deviceId ) ) ) {
			mDevices.erase( deviceId );
		}
	}
}

void MessageWindowManager::closeAllDevices()
{
	lock_guard< recursive_mutex > lock( mRMutex );

	for ( auto& d : mDevices ) {
		closeRegistration( d.second );
	}

	mDevices.clear();
}

// Global system management ---------------------------------------------------

void Device::initialize( HWND rendererWindowId )
{
	MessageWindowManager::instance()->start( rendererWindowId );
}

void Device::shutdown()
{
	MessageWindowManager::destroyInstance();
}

// Global system introspection ------------------------------------------------

vector< SiDevID > Device::getAllDeviceIds()
{
	int nDevices = SiGetNumDevices();
	vector< SiDevID > ids;
	for ( int i = 0; i < nDevices; ++i )
		ids.push_back( i + 1 ); // 0 is the test device

	return ids;
}


// Device class ---------------------------------------------------------------

DeviceRef Device::create( SiDevID deviceId )
{
	return MessageWindowManager::instance()->initDevice( deviceId );
}

Device::Device( SiDevID deviceId, const string &name, const string &port, status status ) :
	mLEDState( true ),
	mDeviceId( deviceId ),
	mName( name ),
	mPort( port ),
	mStatus( status )
{

}

Device::~Device()
{
	MessageWindowManager::instance()->closeDevice( mDeviceId );
}

void Device::update()
{
	SiSpwEvent event;
	while ( mEventQueue.try_pop( &event ) )
	{
		if ( event.type == SI_MOTION_EVENT )
		{
			dispatchMotionEvent( event );
		}
		else if ( event.type == SI_ZERO_EVENT )
		{
			dispatchZeroEvent( event ); /* process 3D mouse zero event */
		}
		else if ( event.type == SI_BUTTON_PRESS_EVENT )
		{
			dispatchButtonDownEvent( event );  /* process button press event */
		}
		else if ( event.type == SI_BUTTON_RELEASE_EVENT )
		{
			dispatchButtonUpEvent( event ); /* process button release event */
		}
		else if ( event.type == SI_DEVICE_CHANGE_EVENT )
		{
			dispatchDeviceChangeEvent( event ); /* process 3D mouse device change event */
		}
		else if ( event.type == SI_CMD_EVENT )
		{
			CI_LOG_V( "3DConnexion SI_CMD_EVENT not handled yet." );
		}
	}
}

void Device::setLED( bool on )
{
	if ( mLEDState != on ) {
		MessageWindowManager::instance()->setDeviceLED( mDeviceId, on );
		mLEDState = on;
	}
}


void Device::queueEvent( const SiSpwEvent & event )
{
	mEventQueue.push( event );
}


void Device::dispatchMotionEvent( const SiSpwEvent & event )
{
	vec3 r(
		event.u.spwData.mData[SI_RX],
		event.u.spwData.mData[SI_RY],
		event.u.spwData.mData[SI_RZ]
		);
	vec3 t(
		event.u.spwData.mData[SI_TX],
		event.u.spwData.mData[SI_TY],
		event.u.spwData.mData[SI_TZ]
		);

	long p = event.u.spwData.period;

	mMotionSignal.emit( MotionEvent( mDeviceId, r, t, p ) );
}

void Device::dispatchZeroEvent( const SiSpwEvent & event )
{
	mMotionSignal.emit( MotionEvent( mDeviceId, vec3(), vec3(), event.u.spwData.period ) );
}

void Device::dispatchButtonDownEvent( const SiSpwEvent & event )
{

	V3DKey code = event.u.hwButtonEvent.buttonNumber;
	string name = MessageWindowManager::instance()->getDeviceButtonName( mDeviceId, code );
	mButtonDownSignal.emit( ButtonDownEvent( mDeviceId, code, name ) );
}

void Device::dispatchButtonUpEvent( const SiSpwEvent & event )
{
	V3DKey code = event.u.hwButtonEvent.buttonNumber;
	string name = MessageWindowManager::instance()->getDeviceButtonName( mDeviceId, code );

	mButtonUpSignal.emit( ButtonUpEvent( mDeviceId, code, name ) );
}

void Device::dispatchDeviceChangeEvent( const SiSpwEvent & event )
{
	SiDeviceChangeType t = event.u.deviceChangeEventData.type;
	SiDevID did = event.u.deviceChangeEventData.devID;

	mDeviceChangeSignal.emit( DeviceChangeEvent( mDeviceId, t, did ) );
}

// Events ---------------------------------------------------------------------

Event::Event( SiDevID deviceId ) :
	deviceId( deviceId )
{

}

MotionEvent::MotionEvent( SiDevID deviceId, const ci::vec3 & r, const ci::vec3 & t, const long & p ) :
	Event( deviceId ),
	rotation( r ),
	translation( t ),
	timeSinceLast( p )
{
}

ButtonEvent::ButtonEvent( SiDevID deviceId, const V3DKey & c, const std::string & n ) :
	Event( deviceId ),
	code( c ),
	name( n )
{
}

ButtonDownEvent::ButtonDownEvent( SiDevID deviceId, const V3DKey & c, const std::string & n ) :
	ButtonEvent( deviceId, c, n )
{
}

ButtonUpEvent::ButtonUpEvent( SiDevID deviceId, const V3DKey & c, const std::string & n ) :
	ButtonEvent( deviceId, c, n )
{
}

DeviceChangeEvent::DeviceChangeEvent( SiDevID deviceId, const SiDeviceChangeType & t, const SiDevID & did ) :
	Event( deviceId ),
	type( t ),
	deviceId( did )
{
}