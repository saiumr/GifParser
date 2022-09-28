#ifndef  _PARSER_SINGLETON_
#define  _PARSER_SINGLETON_

#include <stdbool.h>
#include <SDL.h>
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _PARSER_ALL* PARSER_ALL;

typedef
bool
(*PARSER_ALL_INIT)(
  const char* title,
  int xpos,
  int ypos,
  int width,
  int height,
  bool fullscreen
);

typedef
bool
(*PARSER_ALL_RUNNING)();

typedef
void
(*PARSER_ALL_HANDLE_EVENTS)();

typedef
void
(*PARSER_ALL_UPDATE)();

typedef
void
(*PARSER_ALL_RENDER)();

typedef
void
(*PARSER_ALL_CLEAN)();

typedef
void
(*PARSER_ALL_QUIT)();

typedef
int
(*PARSER_ALL_DELAY)();



typedef struct {
  // Header Block [6Bytes]
  char CharG;       // 'G'
  char CharI;       // 'I'
  char CharF;       // 'F'   Signature
  char Version[3];  // "89a" or "87a"
  
  // Logical Screen Descriptor [7Bytes]
  unsigned short CanvasWidth;    // ignore
  unsigned short CanvasHeight;   // ignore
  struct {
    char SizeOfGlobalTable : 3;
    char SortFlag : 1;
    char ColorResolution : 3;
    char GlobalColorTableFlag : 1;
  } PackedFiled;
  
  char BackgroundColorIndex;     // ignore
  char PixelAspectRatio;

  // Global Color Table
  

}PARSER_GIF_STRUCT;

// Public Members
// This structure should be singleton, so...there isn't pointer to itself in member functions parameters.
struct _PARSER_ALL
{
  PARSER_ALL_INIT              Init;
  PARSER_ALL_RUNNING           Running;
  PARSER_ALL_HANDLE_EVENTS     HandleEvents;
  PARSER_ALL_UPDATE            Update;
  PARSER_ALL_RENDER            Render;
  PARSER_ALL_DELAY             GetDelayTime;
  PARSER_ALL_CLEAN             Clean;
  PARSER_ALL_QUIT              Quit;
};

PARSER_ALL ParserInstance();

#endif  // _ALL_