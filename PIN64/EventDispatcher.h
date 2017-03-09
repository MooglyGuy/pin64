#pragma once

#include <cstdint>
#include <vector>
#include <SDL.h>

enum EventType : uint32_t {
	EventType_Quit,
	EventType_Pause,
	EventType_KeyDown,

	EventType_Count
};

class InputEvent {
public:
	InputEvent(EventType type)
		: mType(type) {
	}

	EventType GetType() const { return mType; }

protected:
	EventType mType;
};

class QuitEvent : public InputEvent {
public:
	QuitEvent()
		: InputEvent(EventType_Quit) {
	}
};

class PauseEvent : public InputEvent {
public:
	PauseEvent()
		: InputEvent(EventType_Pause) {
	}
};

class KeyDownEvent : public InputEvent {
public:
	KeyDownEvent(SDL_Keysym key)
		: InputEvent(EventType_KeyDown)
		, m_key(key) {
	}

	SDL_Keysym key() const { return m_key; }

private:
	SDL_Keysym m_key;
};

class EventListener {
public:
	virtual ~EventListener() { }

	virtual bool HandleEvent(InputEvent event) = 0;
};

class EventDispatcher {
public:
	EventDispatcher() { }
	
	void PollEvents();

	void DispatchEvent(InputEvent event);

	void RegisterListener(EventListener* listener);
	void UnregisterListener(EventListener* listener);

private:
	std::vector<EventListener*> mListeners;
};