#include <stdio.h>
#include <SDL3/SDL.h>

int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("Init failed: %s\n", SDL_GetError());
    }

    printf("goodbye!\n");

    return 0;
}
