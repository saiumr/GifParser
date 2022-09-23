#ifndef _GIF_PARSER_
#define _GIF_PARSER_

#include "gif.h"

typedef struct {
	UINT32    w, h;
	UINT32    count;
	CHAR      **frames;
	UINT32    delays;
} GIF_PARSER;


#endif