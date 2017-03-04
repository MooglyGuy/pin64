#include <iostream>
#include <SDL.h>
#include "Logger.h"
#include "Game.h"

int main(int, char**) {
	Logger::StartLogging("run.log");
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		Logger::Log("SDL Init Error: %s\n", SDL_GetError());
		return 1;
	}

	Game* game = new Game();
	if (game->Initialize())
		game->Run();

	SDL_Quit();
	Logger::StopLogging();
	return 0;
}
