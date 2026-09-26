/* Incremental-output verification harness.
 *
 * A player does not have a canvas on screen: it has a texture that keeps its
 * contents between frames. So the only thing that has to be written each frame is
 * GIFCanvasDirtyRect(animation, i) - and if that rectangle is even one pixel too
 * small, the screen silently goes wrong somewhere later in the animation.
 *
 * This harness simulates exactly that destination: it keeps a persistent "screen"
 * buffer, updates only the dirty rectangle out of the canvas with
 * GIFCanvasOutputRect, and writes the *whole* screen out each frame. Diffing those
 * dumps against the Python reference compositor therefore tests the incremental
 * path end to end, not just the canvas.
 *
 * usage: dirtycheck <gif> <out_dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include "gif_parser.h"

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <gif> <out_dir>\n", argv[0]);
        return 2;
    }

    IMG_ANIMATION *a = NULL;
    if (!GIFParserGetAnimationFromFile(argv[1], &a))
    {
        printf("PARSE_FAILED\n");
        return 1;
    }

    GIF_CANVAS canvas;
    if (!GIFCanvasCreate(&canvas, a))
    {
        printf("CANVAS_FAILED\n");
        GIFParserClearAnimation(a);
        return 1;
    }

    size_t n = (size_t)a->width * (size_t)a->height;
    IMG_FRAME *screen = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * n);
    if (screen == NULL)
    {
        GIFCanvasDestroy(&canvas);
        GIFParserClearAnimation(a);
        return 1;
    }
    /* Deliberately left uninitialised: frame 0's dirty rectangle must cover the
       whole canvas precisely because the destination's contents are unknown. */

    unsigned long long dirty_pixels = 0;
    unsigned long long whole_frames = 0;

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
            GIFCanvasOutputRect(&canvas, screen + dirty.top * a->width + dirty.left,
                                a->width, &dirty);
            dirty_pixels += (unsigned long long)dirty.width * dirty.height;
        }
        else
        {
            dirty.width = 0;
            dirty.height = 0;
        }
        whole_frames += (unsigned long long)n;

        GIFCanvasApplyDisposal(&canvas, a, i);

        /* "-" measures the dirty-rectangle ratio without writing dumps */
        if (argv[2][0] == '-' && argv[2][1] == '\0')
        {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/f%04lu.rgb", argv[2], (unsigned long)i);
        FILE *fp = fopen(path, "wb");
        if (fp == NULL)
        {
            printf("OPEN_FAILED %s\n", path);
            break;
        }
        for (size_t p = 0; p < n; ++p)
        {
            fputc(screen[p].r, fp);
            fputc(screen[p].g, fp);
            fputc(screen[p].b, fp);
        }
        fclose(fp);
    }

    printf("DIRTY %s frames=%lu canvas=%lux%lu dirty_pixels=%llu full_pixels=%llu ratio=%.2f%%\n",
           argv[1], (unsigned long)a->count, (unsigned long)a->width, (unsigned long)a->height,
           dirty_pixels, whole_frames,
           (whole_frames > 0) ? 100.0 * (double)dirty_pixels / (double)whole_frames : 0.0);

    free(screen);
    GIFCanvasDestroy(&canvas);
    GIFParserClearAnimation(a);
    return 0;
}
