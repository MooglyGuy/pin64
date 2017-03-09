#include <SDL.h>

#include "Logger.h"
#include "Game.h"

Game::Game()
	: mInitialized(false)
	, mRunning(false)
	, mPaused(false)
	, mCurrTime(0)
	, mLastTime(0)
	, mWindow(nullptr)
	, mRenderer(nullptr)
	, mFramebuffer(nullptr)
	, mFB(nullptr)
	, mEventDispatcher(nullptr)
	, mRDRAM(nullptr)
	, mHiddenRAM(nullptr)
	, mCapture(nullptr)
	, mRDP(nullptr) {
}

Game::~Game() {
	if (mInitialized) {
		SDL_DestroyRenderer(mRenderer);
		SDL_DestroyWindow(mWindow);
		delete mEventDispatcher;
		delete mCapture;
	}
}

bool Game::Initialize() {
	mWindow = SDL_CreateWindow("PIN64 v0.00", 100, 100, 640, 480, SDL_WINDOW_SHOWN);
	if (!mWindow) {
		Logger::Log("Call to SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}

	mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!mRenderer) {
		SDL_DestroyWindow(mWindow);
		Logger::Log("Call to SDL_CreateRenderer failed: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}

	mEventDispatcher = new EventDispatcher();
	mEventDispatcher->RegisterListener(this);
	
	mInitialized = true;

	mFramebuffer = SDL_CreateTexture(mRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 640, 480);

	mFB = std::make_unique<uint8_t[]>(8 * 1024 * 1024);
	mRDRAM = std::make_unique<uint8_t[]>(8 * 1024 * 1024);
	mHiddenRAM = std::make_unique<uint8_t[]>(8 * 1024 * 1024);
	
	mCapture = new pin64_t();
	mCapture->play(0);

	mRDP = new n64_rdp((uint32_t*)mRDRAM.get());
	mRDP->init_internal_state(mCapture);

	return true;
}

static double mix(double a, double b, double factor) {
	return a + (b - a) * factor;
}

void Game::Render(double delta) {
	if (!mPaused) {
		if (!mCapture->playing()) {
			mCapture->play(0);
		} else {
			if (mRDP->commands_available()) {
				mRDP->process_command();
				mCapture->next_command();
			} else {
				mCapture->mark_frame(running_machine());
				
				mRDP->screen_update(reinterpret_cast<uint32_t*>(mFB.get()));

				SDL_UpdateTexture(mFramebuffer, nullptr, mFB.get(), 640 * 4);

				SDL_SetRenderDrawColor(mRenderer, 0, 0, 0, 0xff);
				SDL_RenderClear(mRenderer);
				SDL_RenderCopy(mRenderer, mFramebuffer, nullptr, nullptr);
				SDL_RenderPresent(mRenderer);
			}
		}
	}
}

void Game::Run() {
	mRunning = true;

	while (mRunning) {
		
		mLastTime = mCurrTime;
		mCurrTime = SDL_GetPerformanceCounter();
		uint64_t timeDelta = mCurrTime - mLastTime;
		double secondsDelta = double(timeDelta * 1000) / SDL_GetPerformanceFrequency();
		Render(secondsDelta);

		mEventDispatcher->PollEvents();
	}
}

bool Game::HandleEvent(InputEvent event) {
	switch (event.GetType()) {
	case EventType_Quit:
		mRunning = false;
		return true;
	case EventType_KeyDown:
	{
		KeyDownEvent& keyEvent = reinterpret_cast<KeyDownEvent&>(event);
		if (keyEvent.key().sym == SDLK_p) {
			mEventDispatcher->DispatchEvent(PauseEvent());
		}
	}

	case EventType_Pause:
		mPaused = !mPaused;
		return true;
	}
	return false;
}