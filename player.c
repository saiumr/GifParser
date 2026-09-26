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
    /* uploaded-pixel accounting, so the dirty-rectangle saving is observable */
    unsigned long long dirty_pixels;
    unsigned long long full_pixels;
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
    player->dirty_pixels = 0;
    player->full_pixels = 0;

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

    SDL_Quit();
}

/* Convert one rectangle of the canvas straight into the locked texture.
 *
 * This is the whole point of the dirty-rectangle path: the loop touches only the
 * pixels that can have changed - no full-canvas conversion pass, no full-canvas
 * upload, and no full-canvas staging buffer (the W*H*4 `pixels` array this player
 * used to keep is gone entirely). Reading `canvas->rgb` directly is deliberate:
 * going through GIFCanvasOutputRect first would add a second pass over the same
 * pixels for no benefit. */
static void ConvertCanvasRectToRGBA(const GIF_CANVAS *canvas, const GIF_RECT *rect,
                                    uint32_t *dst, size_t dst_pitch)
{
    const IMG_FRAME *src = canvas->rgb + rect->top * canvas->width + rect->left;
    for (UINTN row = 0; row < rect->height; ++row)
    {
        uint32_t *out = dst + row * dst_pitch;
        const IMG_FRAME *in = src + (size_t)row * canvas->width;
        for (UINTN col = 0; col < rect->width; ++col)
        {
            out[col] =
                ((uint32_t)in[col].r << 24) |
                ((uint32_t)in[col].g << 16) |
                ((uint32_t)in[col].b << 8)  |
                0xFF;
        }
    }
}

static bool SDLPlayerUploadDirty(SDL_PLAYER *player, const GIF_CANVAS *canvas,
                                 const GIF_RECT *rect)
{
    SDL_Rect region;
    region.x = (int)rect->left;
    region.y = (int)rect->top;
    region.w = (int)rect->width;
    region.h = (int)rect->height;

    void *dst = NULL;
    int pitch = 0;
    if (!SDL_LockTexture(player->texture, &region, &dst, &pitch))
    {
        fprintf(stderr, "SDL_LockTexture failed: %s\n", SDL_GetError());
        return false;
    }

    ConvertCanvasRectToRGBA(canvas, rect, (uint32_t *)dst, (size_t)pitch / sizeof(uint32_t));

    SDL_UnlockTexture(player->texture);

    player->dirty_pixels += (unsigned long long)rect->width * rect->height;
    return true;
}

static bool SDLPlayerPresentTexture(SDL_PLAYER *player)
{
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
        (unsigned)animation->width,
        (unsigned)animation->height,
        (unsigned)animation->count);

    SDL_PLAYER player = {0};
    if (!SDLPlayerInit(&player, (int)animation->width, (int)animation->height))
    {
        GIFParserClearAnimation(animation);
        return 1;
    }

    // One canvas, and no full-canvas frame buffer: the picture is uploaded
    // straight out of the canvas, one dirty rectangle at a time.
    GIF_CANVAS canvas;
    if (!GIFCanvasCreate(&canvas, animation))
    {
        fprintf(stderr, "Failed to create the compositing canvas\n");
        SDLPlayerDestroy(&player);
        GIFParserClearAnimation(animation);
        return 1;
    }

    bool running = true;
    while (running)
    {
        GIFCanvasReset(&canvas, animation);
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

            if (!GIFCanvasCompose(&canvas, animation, i))
            {
                fprintf(stderr, "Failed to composite frame %u\n", (unsigned)i);
                running = false;
                break;
            }

            /* Upload before the disposal runs: disposal describes the canvas the
               *next* frame starts from, and the screen has to show this frame as
               composited. */
            GIF_RECT dirty;
            player.full_pixels += (unsigned long long)animation->width * animation->height;
            if (GIFCanvasDirtyRect(animation, i, &dirty))
            {
                if (!SDLPlayerUploadDirty(&player, &canvas, &dirty))
                {
                    running = false;
                    break;
                }
            }
            GIFCanvasApplyDisposal(&canvas, animation, i);

            if (!SDLPlayerPresentTexture(&player))
            {
                running = false;
                break;
            }

            UINT32 delay = animation->frames[i].delay_ms;
            if (delay < 10)
            {
                delay = 10;
            }

            SDL_Delay(delay);
        }
    }

    if (player.full_pixels > 0)
    {
        printf("uploaded %llu of %llu pixels (%.1f%% of the full-canvas cost)\n",
               player.dirty_pixels, player.full_pixels,
               100.0 * (double)player.dirty_pixels / (double)player.full_pixels);
    }

    GIFCanvasDestroy(&canvas);
    SDLPlayerDestroy(&player);
    GIFParserClearAnimation(animation);
    return 0;
}
