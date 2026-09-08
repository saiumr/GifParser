all: parser creator

parser:
	gcc main.c gif_parser.c ./lzw/lzw.c ./lzw/lzw_table.c ./lzw/lzw_bits.c ./lzw/darray.c -g -o parser.exe -I"./"

creator:
	gcc gif_creator.c ./lzw/lzw.c ./lzw/lzw_table.c ./lzw/lzw_bits.c ./lzw/darray.c -o creator.exe -I"./"

player:
	gcc player.c -o player.exe -ISDL3/include -LSDL3/lib -lSDL3
	copy SDL3\bin\SDL3.dll .\

clean:
	del *.exe *.raw .\frames\*.bmp SDL3.dll

.PHONY: clean