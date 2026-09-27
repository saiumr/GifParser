/* Verification harness for the streamed animation API.
 *
 * Composites frames through the canvas (so it exercises GIFCanvasCompose /
 * Render rather than the one-shot helper) and writes raw RGB, so the output can
 * be diffed byte-for-byte against the Python reference compositor.
 *
 * usage: dumpanim <gif> <out_dir>
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

    printf("w=%lu h=%lu count=%lu\n", (unsigned long)a->width, (unsigned long)a->height,
           (unsigned long)a->count);
    for (UINTN i = 0; i < a->count; ++i)
    {
        printf("delay[%lu]=%u\n", (unsigned long)i, (unsigned)a->frames[i].delay_ms);
    }

    GIF_CANVAS canvas;
    if (!GIFCanvasCreate(&canvas, a))
    {
        printf("CANVAS_FAILED\n");
        return 1;
    }
    size_t n = (size_t)a->width * (size_t)a->height;
    IMG_FRAME *rgb = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * n);
    if (rgb == NULL)
    {
        return 1;
    }

    for (UINTN i = 0; i < a->count; ++i)
    {
        if (!GIFCanvasCompose(&canvas, a, i))
        {
            printf("COMPOSE_FAILED frame %lu\n", (unsigned long)i);
            break;
        }
#ifdef GIF_CANVAS_DEBUG
        printf("[dbg] about to render frame %lu: canvas.rgb[0]=(%u,%u,%u)\n",
               (unsigned long)i, canvas.rgb[0].r, canvas.rgb[0].g, canvas.rgb[0].b);
#endif
        GIFCanvasOutput(&canvas, a, rgb);
        /* disposal changes the canvas the next frame starts from, so it must run
           only after the picture has been read out */
        GIFCanvasApplyDisposal(&canvas, a, i);
#ifdef GIF_CANVAS_DEBUG
        printf("[dbg] rendered frame %lu: rgb[0]=(%u,%u,%u)\n", (unsigned long)i, rgb[0].r, rgb[0].g, rgb[0].b);
#endif

        char path[512];
        snprintf(path, sizeof(path), "%s/f%04lu.rgb", argv[2], (unsigned long)i);
        FILE *fp = fopen(path, "wb");
        if (fp != NULL)
        {
            for (size_t p = 0; p < n; ++p)
            {
                fputc(rgb[p].r, fp);
                fputc(rgb[p].g, fp);
                fputc(rgb[p].b, fp);
            }
            fclose(fp);
        }
    }

    /* also exercise the one-shot helper to make sure it agrees */
    IMG_FRAME *solo = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * n);
    if (solo != NULL && a->count > 0)
    {
        if (GIFParserAnimationComposeFrame(a, a->count - 1, solo))
        {
            printf("oneshot_last_frame=ok\n");
        }
        else
        {
            printf("oneshot_last_frame=FAILED\n");
        }
        free(solo);
    }

    free(rgb);
    GIFCanvasDestroy(&canvas);
    GIFParserClearAnimation(a);
    return 0;
}
