#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "gif_parser.h"

typedef struct
{
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    int width;
    int height;
    uint32_t *pixels;
} SDL_PLAYER;

static bool SDLPlayerInit(SDL_PLAYER *player, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr,
                "SDL_Init failed: %s\n",
                SDL_GetError());

        return false;
    }

    player->window = SDL_CreateWindow(
        "GIF Player",
        width,
        height,
        SDL_WINDOW_RESIZABLE);

    if (player->window == NULL)
    {
        fprintf(stderr,
                "SDL_CreateWindow failed: %s\n",
                SDL_GetError());

        SDL_Quit();
        return false;
    }

    player->renderer = SDL_CreateRenderer(player->window, NULL);

    if (player->renderer == NULL)
    {
        fprintf(stderr,
                "SDL_CreateRenderer failed: %s\n",
                SDL_GetError());

        SDL_DestroyWindow(player->window);
        SDL_Quit();

        return false;
    }

    /*
     * GIF frame use RGB/BGR structure
     * use RGBA8888 firstly
     */
    player->texture = SDL_CreateTexture(
        player->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height);

    if (player->texture == NULL)
    {
        fprintf(stderr,
                "SDL_CreateTexture failed: %s\n",
                SDL_GetError());

        SDL_DestroyRenderer(player->renderer);
        SDL_DestroyWindow(player->window);
        SDL_Quit();

        return false;
    }

    player->width = width;
    player->height = height;

    player->pixels = (uint32_t *)malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (player->pixels == NULL)
    {
        fprintf(stderr, "Allocate framebuffer failed\n");

        SDL_DestroyTexture(player->texture);
        SDL_DestroyRenderer(player->renderer);
        SDL_DestroyWindow(player->window);
        SDL_Quit();

        return false;
    }

    return true;
}

static void SDLPlayerDestroy(SDL_PLAYER *player)
{
    if (player->texture != NULL)
    {
        SDL_DestroyTexture(player->texture);
        player->texture = NULL;
    }

    if (player->renderer != NULL)
    {
        SDL_DestroyRenderer(player->renderer);
        player->renderer = NULL;
    }

    if (player->window != NULL)
    {
        SDL_DestroyWindow(player->window);
        player->window = NULL;
    }

    if (player->pixels != NULL) {
        free(player->pixels);
        player->pixels = NULL;
    }

    SDL_Quit();
}

static void ConvertFrameToRGBA(const IMG_FRAME *frame, uint32_t *pixels, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; ++i)
    {
        pixels[i] =
            ((uint32_t)frame[i].r << 24) |
            ((uint32_t)frame[i].g << 16) |
            ((uint32_t)frame[i].b << 8)  |
            0xFF;
    }
}

static bool SDLPlayerPresentFrame(SDL_PLAYER *player, const IMG_FRAME *frame)
{
    size_t pixel_count = (size_t)player->width * (size_t)player->height;

    ConvertFrameToRGBA(
        frame,
        player->pixels,
        pixel_count);

    if (!SDL_UpdateTexture(
            player->texture,
            NULL,
            player->pixels,
            player->width * sizeof(uint32_t)))
    {
        fprintf(stderr,
                "SDL_UpdateTexture failed: %s\n",
                SDL_GetError());

        return false;
    }

    if (!SDL_RenderClear(player->renderer))
    {
        return false;
    }

    if (!SDL_RenderTexture(
            player->renderer,
            player->texture,
            NULL,
            NULL))
    {
        return false;
    }

    return SDL_RenderPresent(player->renderer);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <gif picture>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    IMG_ANIMATION *animation = NULL;
    if (!GIFParserGetAnimationFromFile(filename, &animation))
    {
        fprintf(stderr, "Failed to parse GIF: %s\n", filename);
        return 1;
    }

    printf("GIF: %ux%u, frames=%u\n",
        (unsigned)animation->w,
        (unsigned)animation->h,
        (unsigned)animation->count);

    SDL_PLAYER player = {0};
    if (!SDLPlayerInit(&player, (int)animation->w, (int)animation->h))
    {
        GIFParserClearAnimation(animation);
        return 1;
    }

    bool running = true;
    while (running)
    {
        for (UINTN i = 0; i < animation->count; ++i)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;
                    break;
                }
            }

            if (!running)
            {
                printf("goodbye!\n");
                break;
            }

            if (!SDLPlayerPresentFrame(&player, animation->frames[i]))
            {
                running = false;
                break;
            }

            UINT32 delay = animation->delays[i];
            if (delay < 10)
            {
                delay = 10;
            }

            SDL_Delay(delay);
        }
    }

    SDLPlayerDestroy(&player);
    GIFParserClearAnimation(animation);
    return 0;
}
