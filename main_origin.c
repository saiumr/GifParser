#include <SDL.h>
#include <SDL_image.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

SDL_Window*     window = NULL;
SDL_Renderer*   renderer = NULL;
SDL_Texture**   animation_texture = NULL;

int main(int argc, char** argv) {
  SDL_Init(SDL_INIT_EVERYTHING);
  window = SDL_CreateWindow("GIF Parser", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_SHOWN);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

  SDL_RWops* file = SDL_RWFromFile("lm.gif", "rb");
  IMG_Animation* animation = IMG_LoadGIFAnimation_RW(file);
  SDL_FreeRW(file);

  animation_texture = (SDL_Texture**)malloc(sizeof(SDL_Texture*) * animation->count);
  for (int i = 0; i < animation->count; ++i) {
    animation_texture[i] = SDL_CreateTextureFromSurface(renderer, animation->frames[i]);
  }

  SDL_Rect src_rect0 = {0, 0, 23, 42};
  SDL_Rect dst_rect0 = {10, 10, 23, 42};
  SDL_Rect src_rect1 = {0, 0, animation->w, animation->h};
  SDL_Rect dst_rect1 = {100, 10, animation->w, animation->h};

  int frame_count = animation->count;
  int animation_delay = *animation->delays;
  IMG_FreeAnimation(animation);

  bool is_quit = false;
  SDL_Event event;
  int frame_index = frame_count;  // for starting from frame 0
  while (!is_quit) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        printf("GoodBye!\n");
        is_quit = true;
      }
      if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_q) {
          printf("GoodBye!\n");
          is_quit = true;
        }
      }
    }

    ++frame_index;
    if (frame_index >= frame_count) {
      frame_index = 0;
    }
    
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, animation_texture[frame_index], &src_rect1, &dst_rect1);  // halt when use create texture and destroy texture
    SDL_RenderPresent(renderer);

    //printf("frame = %d\n", frame_index);

    SDL_Delay(animation_delay);
  }

  for (int i = 0; i < frame_count; ++i) {
    SDL_DestroyTexture(animation_texture[i]);
  }
  free(animation_texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  IMG_Quit();
  SDL_Quit();
  return 0;
}