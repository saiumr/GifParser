/* Headless verification of the SDL upload path.
 *
 * build_check/dirtycheck.c proves the dirty *rectangle* is right, but it updates a
 * plain memory buffer. The player updates an SDL streaming texture, and that has
 * its own assumption to check: locking only a region must leave the rest of the
 * texture exactly as it was. If a backend clobbered the untouched area, the screen
 * would look correct for a while and then dissolve into garbage - and no
 * pixel comparison of the canvas would ever notice.
 *
 * So this harness runs the real thing: it drives SDL with the dummy video driver
 * and the software renderer, uploads only GIFCanvasDirtyRect() each frame through
 * SDL_LockTexture, renders the texture, reads the whole target back with
 * SDL_RenderReadPixels and compares it pixel for pixel with the canvas. The screen
 * it checks is what SDL would have shown.
 *
 * usage: sdlcheck <gif> [stride]
 *        stride > 1 samples every Nth frame (for large animations)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "gif_parser.h"

static unsigned long long mismatches = 0;
static unsigned long long compared = 0;

static void ConvertRectToRGBA(const GIF_CANVAS *canvas, const GIF_RECT *rect,
                              uint32_t *dst, size_t dst_pitch)
{
    const IMG_FRAME *src = canvas->rgb + rect->top * canvas->width + rect->left;
    for (UINTN row = 0; row < rect->height; ++row)
    {
        uint32_t *out = dst + row * dst_pitch;
        const IMG_FRAME *in = src + (size_t)row * canvas->width;
        for (UINTN col = 0; col < rect->width; ++col)
        {
            out[col] = ((uint32_t)in[col].r << 24) | ((uint32_t)in[col].g << 16) |
                       ((uint32_t)in[col].b << 8) | 0xFF;
        }
    }
}

/* What the screen shows must equal the canvas everywhere, not just inside the
   rectangle that was uploaded this frame. */
static void CompareScreenWithCanvas(const GIF_CANVAS *canvas, const uint32_t *screen,
                                    size_t screen_pitch, const char *what)
{
    for (UINTN y = 0; y < canvas->height; ++y)
    {
        const uint32_t *row = (const uint32_t *)((const unsigned char *)screen + y * screen_pitch);
        const IMG_FRAME *want = canvas->rgb + (size_t)y * canvas->width;
        for (UINTN x = 0; x < canvas->width; ++x)
        {
            uint32_t expected = ((uint32_t)want[x].r << 24) | ((uint32_t)want[x].g << 16) |
                                ((uint32_t)want[x].b << 8) | 0xFF;
            ++compared;
            if (row[x] != expected)
            {
                if (mismatches == 0)
                {
                    printf("FIRST_DIFF %s at (%lu,%lu): screen=%08lX canvas=%08lX\n", what,
                           (unsigned long)x, (unsigned long)y, (unsigned long)row[x],
                           (unsigned long)expected);
                }
                ++mismatches;
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <gif> [stride]\n", argv[0]);
        return 2;
    }
    UINTN stride = (argc > 2) ? (UINTN)strtoul(argv[2], NULL, 10) : 1;
    if (stride == 0)
    {
        stride = 1;
    }

    IMG_ANIMATION *a = NULL;
    if (!GIFParserGetAnimationFromFile(argv[1], &a))
    {
        printf("PARSE_FAILED\n");
        return 1;
    }

    /* No window on anyone's desktop: dummy video + software renderer. */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        printf("SDL_INIT_FAILED %s\n", SDL_GetError());
        GIFParserClearAnimation(a);
        return 3;
    }

    SDL_Window *window = SDL_CreateWindow("sdlcheck", (int)a->width, (int)a->height, 0);
    SDL_Renderer *renderer = (window != NULL) ? SDL_CreateRenderer(window, "software") : NULL;
    SDL_Texture *texture = (renderer != NULL)
                               ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                   SDL_TEXTUREACCESS_STREAMING, (int)a->width,
                                                   (int)a->height)
                               : NULL;
    if (texture == NULL)
    {
        printf("SDL_SETUP_FAILED %s\n", SDL_GetError());
        if (renderer != NULL)
        {
            SDL_DestroyRenderer(renderer);
        }
        if (window != NULL)
        {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        GIFParserClearAnimation(a);
        return 3;
    }

    GIF_CANVAS canvas;
    if (!GIFCanvasCreate(&canvas, a))
    {
        printf("CANVAS_FAILED\n");
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        GIFParserClearAnimation(a);
        return 1;
    }

    void *screen = NULL;
    int screen_pitch = 0;

    unsigned long long uploaded_pixels = 0;
    UINTN checked = 0;
    for (UINTN i = 0; i < a->count; ++i)
    {
        if (!GIFCanvasCompose(&canvas, a, i))
        {
            printf("COMPOSE_FAILED frame %lu\n", (unsigned long)i);
            break;
        }

        GIF_RECT dirty;
        if (GIFCanvasDirtyRect(a, i, &dirty))
        {
            SDL_Rect region = {(int)dirty.left, (int)dirty.top, (int)dirty.width,
                               (int)dirty.height};
            if (!SDL_LockTexture(texture, &region, &screen, &screen_pitch))
            {
                printf("SDL_LOCK_FAILED frame %lu: %s\n", (unsigned long)i, SDL_GetError());
                break;
            }
            ConvertRectToRGBA(&canvas, &dirty, (uint32_t *)screen,
                              (size_t)screen_pitch / sizeof(uint32_t));
            SDL_UnlockTexture(texture);
            uploaded_pixels += (unsigned long long)dirty.width * dirty.height;
        }

        if (i % stride == 0 || i + 1 == a->count)
        {
            /* Render the texture and read the whole target back: this is the screen. */
            if (!SDL_RenderClear(renderer) || !SDL_RenderTexture(renderer, texture, NULL, NULL))
            {
                printf("SDL_RENDER_FAILED frame %lu: %s\n", (unsigned long)i, SDL_GetError());
                break;
            }
            SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);
            if (shot == NULL)
            {
                printf("SDL_READBACK_FAILED frame %lu: %s\n", (unsigned long)i, SDL_GetError());
                break;
            }
            /* the backend picks the readback format; compare in one known layout */
            SDL_Surface *rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA8888);
            SDL_DestroySurface(shot);
            if (rgba == NULL)
            {
                printf("SDL_CONVERT_FAILED frame %lu: %s\n", (unsigned long)i, SDL_GetError());
                break;
            }
            char what[64];
            snprintf(what, sizeof(what), "frame %lu", (unsigned long)i);
            CompareScreenWithCanvas(&canvas, (const uint32_t *)rgba->pixels, (size_t)rgba->pitch,
                                    what);
            SDL_DestroySurface(rgba);
            ++checked;
        }

        GIFCanvasApplyDisposal(&canvas, a, i);
    }

    printf("SDLCHECK %s frames=%lu checked=%lu uploaded_pixels=%llu compared=%llu mismatches=%llu %s\n",
           argv[1], (unsigned long)a->count, (unsigned long)checked, uploaded_pixels, compared,
           mismatches, (mismatches == 0) ? "OK" : "MISMATCH");

    GIFCanvasDestroy(&canvas);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    GIFParserClearAnimation(a);
    return (mismatches == 0) ? 0 : 1;
}
