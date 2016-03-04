#pragma once

#include <windows.h>
#include <si.h>
#include "cinder/Signals.h"
#include "cinder/Vector.h"

namespace connexion {
	class Event
	{

	};

	class MotionEvent : public Event 
	{
	public:
		MotionEvent( const ci::vec3 &r, const ci::vec3 &t, const long &p );

		ci::vec3	rotation;
		ci::vec3	translation;
		long		timeSinceLast;
	};

	class ButtonEvent : public Event
	{
	public:
		ButtonEvent( const V3DKey &c, const std::string &n );

		V3DKey			code;
		std::string		name;
	};

	class ButtonDownEvent : public ButtonEvent
	{
	public:
		ButtonDownEvent( const V3DKey &c, const std::string &n );
	};

	class ButtonUpEvent : public ButtonEvent
	{
	public:
		ButtonUpEvent( const V3DKey &c, const std::string &n );

	};

	class DeviceChangeEvent : public Event
	{
	public:
		DeviceChangeEvent( const SiDeviceChangeType &t, const SiDevID &did );

		SiDeviceChangeType	type;
		SiDevID				deviceId;
	};

	class Device {
	public:
		enum class status : int {
			uninitialized = 0, error = -1, ok = 1
		};

		typedef ci::signals::Signal< void( MotionEvent ) >			motion_signal;
		typedef ci::signals::Signal< void( ButtonDownEvent ) >		button_down_signal;
		typedef ci::signals::Signal< void( ButtonUpEvent ) >		button_up_signal;
		typedef ci::signals::Signal< void( DeviceChangeEvent ) >	device_change_signal;

		Device( HWND rendererWindowId );
		~Device();

		void		update();

		status		getStatus() const;
		void		setLED( bool on );
		bool		getLEDState() const { return mLEDState; }

		motion_signal &			getMotionSignal() { return mMotionSignal; }
		button_down_signal &	getButtonDownSignal() { return mButtonDownSignal; }
		button_up_signal &		getButtonUpSignal() { return mButtonUpSignal; }
		device_change_signal &	getDeviceChangeSignal() { return mDeviceChangeSignal; }
	private:
		bool		mLEDState;

		motion_signal			mMotionSignal;
		button_down_signal		mButtonDownSignal;
		button_up_signal		mButtonUpSignal;
		device_change_signal	mDeviceChangeSignal;
	};
}