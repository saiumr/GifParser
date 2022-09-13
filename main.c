#include "parser_singleton.h"

int main(int argc, char** argv) {
  PARSER_ALL parser = ParserInstance();
  
  // Init
  if (parser->Init("GifParser", 100, 100, 800, 600, false) ) {
    while (parser->Running()) {
      int start_time = SDL_GetTicks();

      // HandleEvents
      parser->HandleEvents();
      // Update
      parser->Update();
      // Render 
      parser->Render();
      // Delay
      if ( SDL_GetTicks() - start_time < parser->GetDelayTime() ) {
        SDL_Delay(parser->GetDelayTime() - (SDL_GetTicks() - start_time) );
      }
    }

    // Clean && Quit
    parser->Clean();
  }
  
  return 0;
}