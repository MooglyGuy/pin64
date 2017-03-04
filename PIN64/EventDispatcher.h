#pragma once

#include <cstdint>
#include <vector>

enum EventType : uint32_t {
	EventType_Quit,

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