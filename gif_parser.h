#ifndef _GIF_PARSER_
#define _GIF_PARSER_

#include <stdbool.h>
#include "gif.h"
#include "bmp.h"

typedef GIF_COLOR_TABLE IMG_FRAME;
typedef struct
{
    UINTN w;
    UINTN h;
    UINTN count;
    IMG_FRAME **frames;
    // each frame display time(ms)  frame[0]->delay[0]
    UINT32 *delays;
} IMG_ANIMATION;

BOOL GIFParserGetGifDataFromFile(IN const CHAR *filename, OUT GIF **gif, OUT UINTN *buffer_size);
UINT8 *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size);
BOOL GIFParserClear(IN GIF *gif);

BOOL GIFParserGetAnimationFromFile(IN const CHAR *filename, OUT IMG_ANIMATION **animation);
BOOL GIFParserGetAnimationFromGif(IN GIF *gif, OUT IMG_ANIMATION **animation);
BOOL GIFParserClearAnimation(IN IMG_ANIMATION *animation);

UINT8 *GIFParserAnimationFramesTransformBMP(IN IMG_ANIMATION *animation, IN UINTN frame_index, OUT UINTN *frame_size);

#endif