#include "parser_singleton.h"

// Public member functions
bool ParserInit(const char* title, int x, int y, int w, int h, bool fullscreen);
void ParserUpdate();
bool ParserRunning();
void ParserHandleEvent();
void ParserRender();
void ParserClean();
void ParserQuit();
int  ParserGetDelayTime();

// Private member functions
bool IsKeyDown_(SDL_Scancode key);
void OnKeyDown_();

// Private Members
struct GifFrames
{
  SDL_Texture    **frames;
  int            w;
  int            h;
  int            delays;
  int            frame_count;
};

struct PARSER_PRIVATE_DATA {
  bool              is_running;
  SDL_Window        *window;
  SDL_Renderer      *renderer;
  const Uint8       *key_state;
  struct GifFrames  gif_frames;
  int               current_frame;

};

static struct PARSER_PRIVATE_DATA data;

bool IsKeyDown_(SDL_Scancode key) {
  if (data.key_state == NULL)  return false;
  return data.key_state[key] == 1;
}

void OnKeyDown_() {
  if (IsKeyDown_(SDL_SCANCODE_Q)) {
    printf("byebye!\n");
    ParserQuit();
  }
}

bool ParserInit(const char* title, int x, int y, int w, int h, bool fullscreen) {
  int flag = SDL_WINDOW_SHOWN;
  if (fullscreen) {
    flag = SDL_WINDOW_FULLSCREEN;
  }

  if (SDL_Init(SDL_INIT_VIDEO) == 0) {
    data.window = SDL_CreateWindow(title, x, y, w, h, flag);
    if (data.window != NULL) {
      data.renderer = SDL_CreateRenderer(data.window, -1, SDL_RENDERER_ACCELERATED);
    }
    else {
      return false;
    }

    if (data.renderer != 0) {
      SDL_SetRenderDrawColor(data.renderer, 0, 0, 0, 255);
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }

  SDL_RWops *file = SDL_RWFromFile("lm.gif", "rb");
  IMG_Animation *animation = IMG_LoadGIFAnimation_RW(file);

  data.gif_frames.frame_count = animation->count;
  data.gif_frames.frames = (SDL_Texture**)malloc(sizeof(SDL_Texture*) * animation->count);
  for (int i = 0; i < animation->count; ++i) {
    data.gif_frames.frames[i] = SDL_CreateTextureFromSurface(data.renderer, animation->frames[i]);
  }
  data.gif_frames.w = animation->w;
  data.gif_frames.h = animation->h;
  data.gif_frames.delays = *animation->delays;
  data.is_running = true;

  IMG_FreeAnimation(animation);
  SDL_FreeRW(file);

  return true;
}

bool ParserRunning() {
  return data.is_running;
}

void ParserHandleEvent() {
  SDL_Event event;
  data.key_state = SDL_GetKeyboardState(NULL);
  while ( SDL_PollEvent(&event) ) {
    switch (event.type) {
    case SDL_QUIT:
      ParserQuit();
      break;

    case SDL_KEYDOWN:
      OnKeyDown_();
      break;
    
    default:
      break;
    }

  }
}

void ParserUpdate() {
  ++data.current_frame;
  if (data.current_frame >= data.gif_frames.frame_count) {
    data.current_frame = 0;
  }

}

void ParserRender() {
  SDL_RenderClear(data.renderer);

  SDL_Rect src_rect = {0, 0, data.gif_frames.w, data.gif_frames.h};
  SDL_Rect dst_rect = {10, 10, data.gif_frames.w, data.gif_frames.h};
  SDL_RenderCopy(data.renderer, data.gif_frames.frames[data.current_frame], &src_rect, &dst_rect);

  SDL_RenderPresent(data.renderer);
}

void ParserClean() {
  for (int i = 0; i < data.gif_frames.frame_count; ++i) {
    SDL_DestroyTexture(data.gif_frames.frames[i]);
  }
  SDL_DestroyRenderer(data.renderer);
  SDL_DestroyWindow(data.window);
  SDL_Quit();
}

void ParserQuit() {
  data.is_running = false;
}

int  ParserGetDelayTime() {
  return data.gif_frames.delays;
}

PARSER_ALL ParserInstance() {
  static PARSER_ALL Parser = NULL;

  if (NULL == Parser) {
    // Lock
    Parser = (PARSER_ALL)malloc(sizeof(struct _PARSER_ALL));
    // Unlock
  }

  Parser->Init         = ParserInit;
  Parser->Running      = ParserRunning;
  Parser->HandleEvents = ParserHandleEvent;
  Parser->Update       = ParserUpdate;
  Parser->Render       = ParserRender;
  Parser->Clean        = ParserClean;
  Parser->Quit         = ParserQuit;
  Parser->GetDelayTime = ParserGetDelayTime;
  return Parser;
}
