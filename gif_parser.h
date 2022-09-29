#ifndef _GIF_PARSER_
#define _GIF_PARSER_

#include <stdbool.h>
#include "gif.h"




typedef struct {
	UINT32    w, h;
	UINT32    count;
	CHAR      **frames;
	UINT32    delays;
} GIF_PARSER;

UINTN GetFileSizeByByte(IN FILE *fp);
BOOL  GIFParserGetGifDataFromFile(IN FILE *fp, OUT GIF **gif, OUT UINTN *buffer_size);
CHAR  *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size);
BOOL  GIFParserClear(IN GIF *gif);

BOOL	GIFParserGetParserFromFile(IN FILE *fp, OUT GIF_PARSER *parser);
BOOL	GIFParserGetParserFromGif(IN GIF *gif, OUT GIF_PARSER *parser);
BOOL	GIFParserClearParser(OUT GIF_PARSER *parser);

#endif