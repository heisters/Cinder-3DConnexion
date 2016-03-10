#pragma once

#include <windows.h>
#include <si.h>
#include <thread>
#include <mutex>
#include "cinder/Signals.h"
#include "cinder/Vector.h"
#include "concurrent_queue.h"

namespace connexion {
	class Event
	{
	public:
		Event( SiDevID deviceId );

		SiDevID		deviceId;
	};

	class MotionEvent : public Event 
	{
	public:
		MotionEvent( SiDevID deviceId, const ci::vec3 &r, const ci::vec3 &t, const long &p );

		ci::vec3	rotation;
		ci::vec3	translation;
		long		timeSinceLast;
	};

	class ButtonEvent : public Event
	{
	public:
		ButtonEvent( SiDevID deviceId, const V3DKey &c, const std::string &n );

		V3DKey			code;
		std::string		name;
	};

	class ButtonDownEvent : public ButtonEvent
	{
	public:
		ButtonDownEvent( SiDevID deviceId, const V3DKey &c, const std::string &n );
	};

	class ButtonUpEvent : public ButtonEvent
	{
	public:
		ButtonUpEvent( SiDevID deviceId, const V3DKey &c, const std::string &n );

	};

	class DeviceChangeEvent : public Event
	{
	public:
		DeviceChangeEvent( SiDevID deviceId, const SiDeviceChangeType &t, const SiDevID &did );

		SiDeviceChangeType	type;
		SiDevID				deviceId;
	};

	typedef std::shared_ptr< class Device > DeviceRef;

	class Device {
	public:
		// Types --------------------------------------------------------------
		enum class status : int {
			uninitialized = 0, error = -1, ok = 1
		};

		typedef ci::signals::Signal< void( MotionEvent ) >			motion_signal;
		typedef ci::signals::Signal< void( ButtonDownEvent ) >		button_down_signal;
		typedef ci::signals::Signal< void( ButtonUpEvent ) >		button_up_signal;
		typedef ci::signals::Signal< void( DeviceChangeEvent ) >	device_change_signal;

		// System Initialization ----------------------------------------------

		static void initialize( HWND rendererWindowId );
		static void shutdown();

		// Instantiation ------------------------------------------------------

		static DeviceRef create( SiDevID deviceId );

		static DeviceRef create( SiDevID deviceId, const std::string &name, status status )
		{ return std::make_shared< Device >( deviceId, name, status ); }

		Device( SiDevID deviceId, const std::string &name, status status );
		~Device();

		// Public API ---------------------------------------------------------

		void					update();

		status					getStatus() const { return mStatus; }
		std::string				getName() const { return mName; }
		void					setLED( bool on );
		bool					getLEDState() const { return mLEDState; }
		SiDevID					getDeviceId() const { return mDeviceId; }

		motion_signal &			getMotionSignal() { return mMotionSignal; }
		button_down_signal &	getButtonDownSignal() { return mButtonDownSignal; }
		button_up_signal &		getButtonUpSignal() { return mButtonUpSignal; }
		device_change_signal &	getDeviceChangeSignal() { return mDeviceChangeSignal; }

		// Members ------------------------------------------------------------

	private:
		// Device member vars
		
		status					mStatus;
		std::string				mName;
		bool					mLEDState;
		SiDevID					mDeviceId;

		// Signals

		void					dispatchMotionEvent( const SiSpwEvent & event );
		void					dispatchZeroEvent( const SiSpwEvent & event );
		void					dispatchButtonDownEvent( const SiSpwEvent & event );
		void					dispatchButtonUpEvent( const SiSpwEvent & event );
		void					dispatchDeviceChangeEvent( const SiSpwEvent & event );

		motion_signal			mMotionSignal;
		button_down_signal		mButtonDownSignal;
		button_up_signal		mButtonUpSignal;
		device_change_signal	mDeviceChangeSignal;

		// Multithreading

	public:
		void					queueEvent( const SiSpwEvent &event );

	private:

		concurrent_queue< SiSpwEvent > mEventQueue;
	};
}