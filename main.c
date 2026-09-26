#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gif_parser.h"

int main(int argc, const char **argv)
{
    if (argc < 2)
    {
        printf("Need a gif file name as args.\n");
        return -1;
    }
    const CHAR *src_file = argv[1];
    CHAR *frame_file = (CHAR *)argv[1];
    UINTN buffer_size = 0;
    UINT8 *buffer = NULL;
    GIF *gif = NULL;

    if (!GIFParserGetGifDataFromFile(src_file, &gif, &buffer_size))
    {
        printf("Failed to parse \"%s\".\n", (const char *)src_file);
        return -1;
    }

    // only touch the output file once we know there is something to write
    FILE *new_file = fopen("parser_output.gif", "wb");
    if (new_file == NULL)
    {
        printf("Cannot open parser_output.gif for writing.\n");
        GIFParserClear(gif);
        return -1;
    }

    buffer = GIFParserGetDataBufferFromGif(gif, buffer_size);
    if (buffer != NULL)
    {
        fwrite(buffer, buffer_size, 1, new_file);
    }

    printf("buffer size: %lu B\n", buffer_size);

    IMG_ANIMATION *animation = NULL;
    UINTN frames_written = 0;

    if (!GIFParserGetAnimationFromFile(src_file, &animation))
    {
        printf("Failed to decode animation from \"%s\".\n", (const char *)src_file);
        goto done;
    }

    // Write frames next to ./frames/<basename><index>.bmp. The file name is
    // derived from the last path separator, and the path buffer is sized to hold
    // the "./frames/" prefix plus the base name plus the index plus ".bmp".
    const char *base = strrchr((const char *)frame_file, '\\');
    base = (base != NULL) ? base + 1 : (const char *)frame_file;
    const char *dot = strrchr(base, '.');
    size_t base_len = (dot != NULL) ? (size_t)(dot - base) : strlen(base);

    CHAR filepath[64] = "./frames/";
    if (strlen(filepath) + base_len + 1 >= sizeof(filepath))
    {
        printf("Output name too long for the ./frames/ path buffer.\n");
        goto done;
    }
    strncat((char *)filepath, base, base_len);

    for (UINTN i = 0; i < animation->count; ++i)
    {
        UINTN frame_size = 0;
        // Frames are streamed: each call composites frame i and returns one BMP
        // buffer, so peak memory stays at one canvas plus one frame instead of
        // count * canvas * 3 bytes.
        UINT8 *bmp = GIFParserAnimationFrameBMP(animation, i, &frame_size);
        if (bmp == NULL)
        {
            continue;
        }

        CHAR file[96] = {0};
        snprintf((char *)file, sizeof(file), "%s%lu.bmp", (const char *)filepath, (unsigned long)i);
        FILE *fp = fopen((const char *)file, "wb");
        if (fp == NULL)
        {
            printf("Cannot open \"%s\" for writing.\n", (const char *)file);
            free(bmp);
            continue;
        }
        fwrite(bmp, frame_size, 1, fp);
        fclose(fp);
        free(bmp);
        ++frames_written;
    }

done:
    free(buffer);
    if (new_file != NULL)
    {
        fclose(new_file);
    }
    if (gif != NULL)
    {
        GIFParserClear(gif);
    }
    if (animation != NULL)
    {
        GIFParserClearAnimation(animation);
    }

    printf("- Done - (%lu frame(s) written to ./frames/)\n", frames_written);

    return 0;
}
