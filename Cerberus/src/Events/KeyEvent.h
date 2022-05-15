#pragma once

#include "Event.h"
#include "Core/KeyCodes.h"
namespace cerberus
{
	///////////////////////////////////////////////////////////////////////
	class KeyEvent;
	class KeyPressedEvent;
	class KeyRealeasedEvent;
	class KeyTypedEvent;
	/////////////////////////////////////////////////////////////////////// KeyEvent
	class KeyEvent : public Event
	{
	public:
		inline int GetKeyCode() const { return m_keyCode; }

		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)
	protected:
		KeyEvent(KeyCode keyCode)
			: m_keyCode(keyCode){}
		KeyCode m_keyCode;
	};

	/////////////////////////////////////////////////////////////////////// KeyPressedEvent
	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(const KeyCode keyCode, const KeyCode repeatCount)
			:KeyEvent(keyCode), m_RepeatCount(repeatCount) {}

		inline KeyCode GetRepeatCount() { return m_RepeatCount; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyPressedEvent: " << m_keyCode << " (" << m_RepeatCount << " repeats)";
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyPressed)

	private:
		KeyCode m_RepeatCount;
	};
	/////////////////////////////////////////////////////////////////////// KeyReleasedEvent
	class KeyReleasedEvent : public KeyEvent
	{
	public:
		KeyReleasedEvent(const KeyCode keyCode)
			:KeyEvent(keyCode) {}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyReleasedEvent: " << m_keyCode;
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyReleased)
	};
	/////////////////////////////////////////////////////////////////////// KeyTypedEvent
	class KeyTypedEvent : public KeyEvent
	{
	public:
		KeyTypedEvent(const KeyCode keyCode)
			:KeyEvent(keyCode) {}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyTypedEvent: " << m_keyCode;
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyTyped)
	};
}