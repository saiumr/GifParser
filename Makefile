all: parser creator

parser:
	gcc main.c gif_parser.c ./lzw/lzw.c ./lzw/lzw_table.c ./lzw/lzw_bits.c ./lzw/darray.c -g -o parser.exe -I"./"

creator:
	gcc gif_creator.c ./lzw/lzw.c ./lzw/lzw_table.c ./lzw/lzw_bits.c ./lzw/darray.c -o creator.exe -I"./"


clean:
	del *.exe *.raw .\frames\sun*

.PHONY: clean