# GIF Parser / creator / player
#
# Sources are listed once so every target picks up new files automatically.

CC      := gcc
CFLAGS  := -g -O2 -Wall -I"./"
LZW_SRC := ./lzw/lzw.c ./lzw/lzw_table.c ./lzw/lzw_bits.c ./lzw/darray.c
# the parser proper plus the canvas compositor
CORE_SRC := gif_parser.c gif_canvas.c $(LZW_SRC)

SDL_INC := -I"SDL3/include"
SDL_LIB := -L"SDL3/lib" -lSDL3

# Verification harnesses. They live in build_check/ and are not part of the
# shipped program; `make check` builds them and runs every check the docs quote.
AUDIT      := build_check/dirtycheck.exe build_check/dumpanim.exe \
              build_check/memcheck.exe build_check/leakcheck.exe \
              build_check/sdlcheck.exe
AUDIT_SRC  := gif_parser.c gif_canvas.c $(LZW_SRC)
WRAP_FLAGS := "-Wl,--wrap=malloc" "-Wl,--wrap=calloc" "-Wl,--wrap=realloc" "-Wl,--wrap=free"

all: parser creator

parser: $(CORE_SRC) main.c
	$(CC) main.c $(CORE_SRC) $(CFLAGS) -o parser.exe

creator: gif_creator.c $(LZW_SRC)
	$(CC) gif_creator.c $(LZW_SRC) $(CFLAGS) -o creator.exe

player: $(CORE_SRC) player.c
	$(CC) player.c $(CORE_SRC) $(CFLAGS) $(SDL_INC) $(SDL_LIB) -o player.exe
	copy SDL3\bin\SDL3.dll .\

build_check/dirtycheck.exe: build_check/dirtycheck.c $(AUDIT_SRC)
	$(CC) build_check/dirtycheck.c $(AUDIT_SRC) $(CFLAGS) -o $@

build_check/dumpanim.exe: build_check/dumpanim.c $(AUDIT_SRC)
	$(CC) build_check/dumpanim.c $(AUDIT_SRC) $(CFLAGS) -o $@

build_check/memcheck.exe: build_check/memcheck.c $(AUDIT_SRC)
	$(CC) build_check/memcheck.c $(AUDIT_SRC) $(CFLAGS) -lpsapi -o $@

build_check/leakcheck.exe: build_check/leakcheck.c $(AUDIT_SRC)
	$(CC) build_check/leakcheck.c $(AUDIT_SRC) $(CFLAGS) -no-pie $(WRAP_FLAGS) -o $@

build_check/sdlcheck.exe: build_check/sdlcheck.c $(AUDIT_SRC)
	$(CC) build_check/sdlcheck.c $(AUDIT_SRC) $(CFLAGS) $(SDL_INC) $(SDL_LIB) -o $@
	copy SDL3\bin\SDL3.dll build_check\

audit: $(AUDIT)

check: parser audit
	python build_check/verify_all.py

clean:
	del *.exe *.raw .\frames\*.bmp SDL3.dll
	del build_check\*.exe build_check\SDL3.dll

.PHONY: all parser creator player audit check clean
