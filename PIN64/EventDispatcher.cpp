#include "EventDispatcher.h"
#include <SDL.h>

void EventDispatcher::PollEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event) != 0) {
		if (event.type == SDL_QUIT) {
			DispatchEvent(QuitEvent());
		}
	}
}

void EventDispatcher::RegisterListener(EventListener* listener) {
	if (std::find(mListeners.begin(), mListeners.end(), listener) == mListeners.end()) {
		mListeners.push_back(listener);
	}
}

void EventDispatcher::UnregisterListener(EventListener* listener) {
	auto iter = std::find(mListeners.begin(), mListeners.end(), listener);
	if (iter != mListeners.end()) {
		mListeners.erase(iter);
	}
}

void EventDispatcher::DispatchEvent(InputEvent event) {
	for (EventListener* listener : mListeners) {
		listener->HandleEvent(event);
	}
}
