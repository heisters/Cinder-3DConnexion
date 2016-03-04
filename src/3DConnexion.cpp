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

static Device::status sDeviceStatus = Device::status::uninitialized;
static SiHdl sDeviceHandle;
static HWND sMessageWindowHndl;
static Device::motion_signal sMotionSignal;
static Device::button_down_signal sButtonDownSignal;
static Device::button_up_signal sButtonUpSignal;
static Device::device_change_signal sDeviceChangeSignal;


void dispatchMotionEvent( const SiSpwEvent & event )
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

	sMotionSignal.emit( MotionEvent( r, t, p ) );
}

void dispatchZeroEvent( const SiSpwEvent & event )
{
	sMotionSignal.emit( MotionEvent( vec3(), vec3(), event.u.spwData.period ) );
}

void dispatchButtonDownEvent( const SiSpwEvent & event )
{

	V3DKey code = event.u.hwButtonEvent.buttonNumber;
	SiButtonName name;
	SiGetButtonName( sDeviceHandle, code, &name );

	sButtonDownSignal.emit( ButtonDownEvent( code, name.name ) );
}

void dispatchButtonUpEvent( const SiSpwEvent & event )
{
	V3DKey code = event.u.hwButtonEvent.buttonNumber;
	SiButtonName name;
	SiGetButtonName( sDeviceHandle, code, &name );

	sButtonUpSignal.emit( ButtonUpEvent( code, name.name ) );
}

void dispatchDeviceChangeEvent( const SiSpwEvent & event )
{
	SiDeviceChangeType t = event.u.deviceChangeEventData.type;
	SiDevID did = event.u.deviceChangeEventData.devID;

	sDeviceChangeSignal.emit( DeviceChangeEvent( t, did ) );
}

LRESULT CALLBACK WndMessageProc(
	HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam )
{
	MSG            msg;      /* incoming message to be evaluated */
	BOOL           handled;  /* is message handled yet */
	SiSpwEvent     event;    /* SpaceWare Event */
	SiGetEventData eventData;/* SpaceWare Event Data */

	handled = SPW_FALSE;     /* init handled */

	GetMessage( &msg, NULL, 0, 0 );
	handled = SPW_FALSE;

	/* init Window platform specific data for a call to SiGetEvent */
	SiGetEventWinInit( &eventData, msg.message, msg.wParam, msg.lParam );

	/* check whether msg was a 3D mouse event and process it */
	if ( SiGetEvent( sDeviceHandle, SI_AVERAGE_EVENTS, &eventData, &event ) == SI_IS_EVENT )
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

		handled = SPW_TRUE;              /* 3D mouse event handled */
	}

	/* not a 3D mouse event, let windows handle it */
	if ( handled == SPW_FALSE )
	{
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	return DefWindowProc( hwnd, wm, wParam, lParam );
}

void SbInit( HWND hwnd )
{
	SiOpenData oData;                    /* OS Independent data to open ball  */

										 /*init the SpaceWare input library */
	if ( SiInitialize() == SPW_DLL_LOAD_ERROR )
	{
		throw exception( "Could not load 3DConnexion DLLs" );
	}

	SiOpenWinInit( &oData, hwnd ); /* init Win. platform specific data  */
	SiSetUiMode( sDeviceHandle, SI_UI_ALL_CONTROLS ); /* Config SoftButton Win Display */

											   /* open data, which will check for device type and return the device handle
											   to be used by this function */
	if ( (sDeviceHandle = SiOpen( "Cinder", SI_ANY_DEVICE, SI_NO_MASK, SI_EVENT, &oData )) == NULL )
	{
		SiTerminate();  /* called to shut down the SpaceWare input library */
		sDeviceStatus = Device::status::error; /* could not open device */
	}
	else
	{
		//SiGrabDevice(devHdl, SPW_TRUE);
		SiDeviceName devName;
		SiGetDeviceName( sDeviceHandle, &devName );
		//mName = devName.name;
		sDeviceStatus = Device::status::ok; /* opened device succesfully */

							  // Disallow other applications from using the device
		SpwRetVal result;
		result = SiGrabDevice( sDeviceHandle, SPW_TRUE /*exclusive*/ );
		if ( result != SPW_NO_ERROR )
		{
			CI_LOG_W( "Could not establish an exclusive claim on " << devName.name );
		}

		// Allow Cinder to manage all button functions
		result = SiSyncSendQuery( sDeviceHandle );
		if ( result != SPW_NO_ERROR )
		{
			CI_LOG_W( "Could not reassign buttons to Cinder control" );
		}

		else
		{
			// Since we don't really know how many buttons there are, just set
			// them until we get an error.
			for ( SPWuint32 bt = 0; result == SPW_NO_ERROR && bt < s3dm::MaxKeyCount; ++bt ) {
				SiSyncSetButtonAssignment( sDeviceHandle, bt, 0 /* the button event is to be passed straight thru */ );
			}

			if ( result == SI_BAD_HANDLE )
			{
				CI_LOG_W( "Bad handle error reassigning buttons to Cinder control" );
			}
		}
	}
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

	/* Initialise 3DxWare access / call to SbInit() */
	SbInit( hWndChild );

	//int bRet;
	//MSG msg;      /* incoming message to be evaluated */
	//while ( bRet = GetMessage( &msg, NULL, 0, 0 ) )
	//{
	//	if ( bRet == -1 ) {
	//		/* handle the error and possibly exit */
	//		return;
	//	}
	//	else {
	//		TranslateMessage( &msg );
	//		DispatchMessage( &msg );
	//	}
	//}

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


Device::Device( HWND rendererWindowId ) :
	mLEDState( true )
{
	if ( sDeviceStatus == status::uninitialized )
	{
		sMessageWindowHndl = CreateHiddenMessageWindow( rendererWindowId );
	}


	sMotionSignal.connect( [&]( MotionEvent event ) { mMotionSignal.emit( event ); } );
	sButtonDownSignal.connect( [&]( ButtonDownEvent event ) { mButtonDownSignal.emit( event ); } );
	sButtonUpSignal.connect( [&]( ButtonUpEvent event ) { mButtonUpSignal.emit( event ); } );
	sDeviceChangeSignal.connect( [&]( DeviceChangeEvent event ) { mDeviceChangeSignal.emit( event ); } );
}

Device::~Device()
{
	// FIXME: will cause pain if Device is instantiated more than once
	SiReleaseDevice( sMessageWindowHndl );
}

void Device::update()
{
	MSG msg;
	while ( PeekMessage( &msg, sMessageWindowHndl, 0, 0, PM_REMOVE ) )
	{
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}
}

void Device::setLED( bool on )
{
	if ( mLEDState != on ) {
		SiSetLEDs( sDeviceHandle, on ? 1 : 0 );
		mLEDState = on;
	}
}

Device::status Device::getStatus() const
{
	return sDeviceStatus;
}