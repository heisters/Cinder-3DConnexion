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
	mStatus( status::uninitialized ),
	mLEDState( true )
{
	SiOpenData oData;                    /* OS Independent data to open ball  */

										 /*init the SpaceWare input library */
	if ( SiInitialize() == SPW_DLL_LOAD_ERROR )
	{
		throw exception( "Could not load 3DConnexion DLLs" );
	}

	SiOpenWinInit( &oData, rendererWindowId ); /* init Win. platform specific data  */
	SiSetUiMode( mHandle, SI_UI_NO_CONTROLS ); /* Config SoftButton Win Display */

											   /* open data, which will check for device type and return the device handle
											   to be used by this function */
	if ( (mHandle = SiOpen( "Cinder", SI_ANY_DEVICE, SI_NO_MASK, SI_EVENT, &oData )) == NULL )
	{
		SiTerminate();  /* called to shut down the SpaceWare input library */
		mStatus = status::error; /* could not open device */
	}
	else
	{
		//SiGrabDevice(devHdl, SPW_TRUE);
		SiDeviceName devName;
		SiGetDeviceName( mHandle, &devName );
		mName = devName.name;
		mStatus = status::ok; /* opened device succesfully */

		// Disallow other applications from using the device
		SpwRetVal result;
		result = SiGrabDevice( mHandle, SPW_TRUE /*exclusive*/ );
		if ( result != SPW_NO_ERROR )
		{
			CI_LOG_W( "Could not establish an exclusive claim on " << mName );
		}

		// Allow Cinder to manage all button functions
		//result = SiSyncSendQuery( mHandle );
		//if ( result != SPW_NO_ERROR )
		//{
		//	CI_LOG_W( "Could not reassign buttons to Cinder control" );
		//}

		//else
		//{
		//	// Since we don't really know how many buttons there are, just set
		//	// them until we get an error.
		//	for ( SPWuint32 bt = 0; result == SPW_NO_ERROR && bt < s3dm::MaxKeyCount; ++bt ) {
		//		SiSyncSetButtonAssignment( mHandle, bt, 0 /* the button event is to be passed straight thru */ );
		//	}

		//	if ( result == SI_BAD_HANDLE )
		//	{
		//		CI_LOG_W( "Bad handle error reassigning buttons to Cinder control" );
		//	}
		//}

//#ifdef TEST_MULTIPLECONNECTIONS
//		SiOpenWinInit( &oData, hWndMain1 );    /* init Win. platform specific data  */
//		devHdl1 = SiOpen( "3DxTest32", SI_ANY_DEVICE, SI_NO_MASK, SI_EVENT, &oData );
//		SiOpenWinInit( &oData, hWndMain2 );    /* init Win. platform specific data  */
//		devHdl2 = SiOpen( "3DxTest32", SI_ANY_DEVICE, SI_NO_MASK, SI_EVENT, &oData );
//		SiOpenWinInit( &oData, hWndMain3 );    /* init Win. platform specific data  */
//		devHdl3 = SiOpen( "3DxTest32", SI_ANY_DEVICE, SI_NO_MASK, SI_EVENT, &oData );
//#endif
	}
}

Device::~Device()
{
	SiReleaseDevice( mHandle );
}

void Device::update()
{
	MSG            msg;      /* incoming message to be evaluated */
	BOOL           handled;  /* is message handled yet */
	SiSpwEvent     event;    /* SpaceWare Event */
	SiGetEventData eventData;/* SpaceWare Event Data */

	handled = SPW_FALSE;     /* init handled */

							 /* start message loop */
	while ( GetMessage( &msg, NULL, 0, 0 ) )
	{
		handled = SPW_FALSE;

		/* init Window platform specific data for a call to SiGetEvent */
		SiGetEventWinInit( &eventData, msg.message, msg.wParam, msg.lParam );

		/* check whether msg was a 3D mouse event and process it */
		if ( SiGetEvent( mHandle, SI_AVERAGE_EVENTS, &eventData, &event ) == SI_IS_EVENT )
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
	}

}

void Device::setLED( bool on )
{
	CI_LOG_D( (on ? "on" : "off") << " " << (mLEDState ? "on" : "off") );
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
