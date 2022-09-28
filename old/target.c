#include <SDL.h>
#include <stdbool.h>

int main(int argc, char** argv) {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = SDL_CreateWindow("GIF Parser", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

  bool is_quit = false;
  SDL_Event event;
  while (!false) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        is_quit = true;
      }
    }

    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_Delay(40);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}