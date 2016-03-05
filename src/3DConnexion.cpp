#include "3DConnexion.h"

#include <exception>
#include <WinUser.h>
#include <tchar.h>

#include <spwmacro.h>
#include <siapp.h>
#include <virtualkeys.hpp>

#include "cinder/Log.h"

using namespace std;
using namespace connexion;
using namespace cinder;

static HWND sMessageWindowHndl = NULL;
static Device* sCurrentDevice = nullptr;


LRESULT CALLBACK WndMessageProc(
	HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam )
{
	MSG				msg;
	SiSpwEvent		event;    /* SpaceWare Event */
	SiGetEventData	eventData;/* SpaceWare Event Data */

	msg.hwnd = hwnd;
	msg.message = wm;
	msg.lParam = lParam;
	msg.wParam = wParam;


	/* init Window platform specific data for a call to SiGetEvent */
	SiGetEventWinInit( &eventData, wm, wParam, lParam );

	/* check whether msg was a 3D mouse event and process it */
	if ( sCurrentDevice && SiGetEvent( sCurrentDevice->getDeviceHandle(), SI_AVERAGE_EVENTS, &eventData, &event ) == SI_IS_EVENT )
	{
		if ( event.type == SI_MOTION_EVENT )
		{
			sCurrentDevice->dispatchMotionEvent( event );
		}
		else if ( event.type == SI_ZERO_EVENT )
		{
			sCurrentDevice->dispatchZeroEvent( event ); /* process 3D mouse zero event */
		}
		else if ( event.type == SI_BUTTON_PRESS_EVENT )
		{
			sCurrentDevice->dispatchButtonDownEvent( event );  /* process button press event */
		}
		else if ( event.type == SI_BUTTON_RELEASE_EVENT )
		{
			sCurrentDevice->dispatchButtonUpEvent( event ); /* process button release event */
		}
		else if ( event.type == SI_DEVICE_CHANGE_EVENT )
		{
			sCurrentDevice->dispatchDeviceChangeEvent( event ); /* process 3D mouse device change event */
		}
		else if ( event.type == SI_CMD_EVENT )
		{
			CI_LOG_V( "3DConnexion SI_CMD_EVENT not handled yet." );
		}

		return 0;
	}

	return DefWindowProc( hwnd, wm, wParam, lParam );
}

HWND CreateHiddenMessageWindow( HWND parent )
{
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof( WNDCLASSEX );
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndMessageProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = GetModuleHandle( NULL );
	wcex.hIcon = NULL;
	wcex.hCursor = LoadCursor( NULL, IDC_ARROW );
	wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = L"HiddenMessageWindowClass";
	wcex.hIconSm = NULL;
	RegisterClassEx( &wcex );

	/* Create a hidden window owned by our process and parented to the console window */
	HWND hWndChild = CreateWindow( wcex.lpszClassName, L"HiddenMessageWindow", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, wcex.hInstance, NULL );

	if ( SiInitialize() == SPW_DLL_LOAD_ERROR )
	{
		throw exception( "Could not load 3DConnexion DLLs" );
	}

	CI_LOG_I( "3DConnexion driver initialized. " << SiGetNumDevices() << " devices attached." );

	return hWndChild;
}


MotionEvent::MotionEvent( const ci::vec3 & r, const ci::vec3 & t, const long & p ) :
	rotation( r ),
	translation( t ),
	timeSinceLast( p )
{
}

ButtonEvent::ButtonEvent( const V3DKey & c, const std::string & n ) :
	code( c ),
	name( n )
{
}

ButtonDownEvent::ButtonDownEvent( const V3DKey & c, const std::string & n ) :
	ButtonEvent( c, n )
{
}

ButtonUpEvent::ButtonUpEvent( const V3DKey & c, const std::string & n ) :
	ButtonEvent( c, n )
{
}

DeviceChangeEvent::DeviceChangeEvent( const SiDeviceChangeType & t, const SiDevID & did ) :
	type( t ),
	deviceId( did )
{
}


void Device::initialize( HWND rendererWindowId )
{
	if ( sMessageWindowHndl == NULL )
	{
		sMessageWindowHndl = CreateHiddenMessageWindow( rendererWindowId );
	}
}

void Device::shutdown()
{
	SiReleaseDevice( sMessageWindowHndl );
}


Device::Device( SiDevID deviceId ) :
	mLEDState( true ),
	mStatus( status::uninitialized )
{
	SiOpenData oData;                    /* OS Independent data to open ball  */

	SiOpenWinInit( &oData, sMessageWindowHndl ); /* init Win. platform specific data  */
	SiSetUiMode( mHandle, SI_UI_ALL_CONTROLS ); /* Config SoftButton Win Display */

													  /* open data, which will check for device type and return the device handle
													  to be used by this function */
	if ( (mHandle = SiOpen( "Cinder", deviceId, NULL, SI_EVENT, &oData )) == NULL )
	{
		SiTerminate();  /* called to shut down the SpaceWare input library */
		mStatus = Device::status::error; /* could not open device */
	}
	else
	{
		//SiGrabDevice(devHdl, SPW_TRUE);
		SiDeviceName devName;
		SiGetDeviceName( mHandle, &devName );
		mName = devName.name;
		mStatus = Device::status::ok; /* opened device succesfully */

		// Disallow other applications from using the device
		SpwRetVal result;
		result = SiGrabDevice( mHandle, SPW_TRUE /*exclusive*/ );
		if ( result != SPW_NO_ERROR )
		{
			CI_LOG_W( "Could not establish an exclusive claim on " << devName.name );
		}

		// Allow Cinder to manage all button functions
		result = SiSyncSendQuery( mHandle );
		if ( result != SPW_NO_ERROR )
		{
			CI_LOG_W( "Could not reassign buttons to Cinder control" );
		}

		else
		{
			// Since we don't really know how many buttons there are, just set
			// them until we get an error.
			for ( SPWuint32 bt = 0; result == SPW_NO_ERROR && bt < s3dm::MaxKeyCount; ++bt ) {
				SiSyncSetButtonAssignment( mHandle, bt, 0 /* the button event is to be passed straight thru */ );
			}

			if ( result == SI_BAD_HANDLE )
			{
				CI_LOG_W( "Bad handle error reassigning buttons to Cinder control" );
			}
		}
	}
}

Device::~Device()
{
}

void Device::update()
{
	sCurrentDevice = this;

	MSG msg;
	while ( PeekMessage( &msg, sMessageWindowHndl, 0, 0, PM_REMOVE ) )
	{
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	sCurrentDevice = nullptr;
}

void Device::setLED( bool on )
{
	if ( mLEDState != on ) {
		SiSetLEDs( mHandle, on ? 1 : 0 );
		mLEDState = on;
	}
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

	mMotionSignal.emit( MotionEvent( r, t, p ) );
}

void Device::dispatchZeroEvent( const SiSpwEvent & event )
{
	mMotionSignal.emit( MotionEvent( vec3(), vec3(), event.u.spwData.period ) );
}

void Device::dispatchButtonDownEvent( const SiSpwEvent & event )
{

	V3DKey code = event.u.hwButtonEvent.buttonNumber;
	SiButtonName name;
	SiGetButtonName( mHandle, code, &name );

	mButtonDownSignal.emit( ButtonDownEvent( code, name.name ) );
}

void Device::dispatchButtonUpEvent( const SiSpwEvent & event )
{
	V3DKey code = event.u.hwButtonEvent.buttonNumber;
	SiButtonName name;
	SiGetButtonName( mHandle, code, &name );

	mButtonUpSignal.emit( ButtonUpEvent( code, name.name ) );
}

void Device::dispatchDeviceChangeEvent( const SiSpwEvent & event )
{
	SiDeviceChangeType t = event.u.deviceChangeEventData.type;
	SiDevID did = event.u.deviceChangeEventData.devID;

	mDeviceChangeSignal.emit( DeviceChangeEvent( t, did ) );
}