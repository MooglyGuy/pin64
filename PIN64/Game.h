#pragma once

#include <memory>

#include "EventDispatcher.h"
#include "video/n64.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

class Game : public EventListener {
public:
	Game();
	virtual ~Game();

	bool Initialize();
	void Run();

	// EventListener
	bool HandleEvent(InputEvent event) override;

private:
	void Render(double delta);

	bool mInitialized;
	bool mRunning;
	
	uint64_t mCurrTime;
	uint64_t mLastTime;
	
	SDL_Window* mWindow;
	SDL_Renderer* mRenderer;
	SDL_Texture* mFramebuffer;
	std::unique_ptr<uint8_t[]> mFB;

	EventDispatcher* mEventDispatcher;

	std::unique_ptr<uint8_t[]> mRDRAM;
	std::unique_ptr<uint8_t[]> mHiddenRAM;
	n64_rdp* mRDP;
};
