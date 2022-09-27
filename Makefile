SDL2_DIR = "D:/exlibs/SDL_lib/SDL2/include/SDL2"
SDL2_IMAGE_DIR = "D:/exlibs/SDL_lib/SDL2_image/include/SDL2"
SDL2_LIB = "D:/exlibs/SDL_lib/SDL2/lib"
SDL2_IMAGE_LIB = "D:/exlibs/SDL_lib/SDL2_image/lib"

all: parser creator target main

parser:
	gcc gif_parser.c lzw/lzw.c lzw/lzw_table.c lzw/lzw_bits.c lzw/darray.c -g -o parser.exe -I"./"

creator:
	gcc gif_creator.c lzw/lzw.c lzw/lzw_table.c lzw/lzw_bits.c lzw/darray.c -o creator.exe -I"./"
	
target:
	gcc target.c -O3 -o target.exe -lmingw32 -lSDL2main -lSDL2 -L${SDL2_LIB} -I${SDL2_DIR}

main:
	gcc main.c parser_singleton.c -o main.exe -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -L${SDL2_LIB} -L${SDL2_IMAGE_LIB} -I${SDL2_DIR} -I${SDL2_IMAGE_DIR}

clean:
	del *.exe *.raw

.PHONY: clean