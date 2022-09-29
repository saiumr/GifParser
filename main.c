#include <stdio.h>
#include <stdlib.h>
#include "gif_parser.h"

int main(int argc, const char** argv) {
	if (argc < 2) {
		printf("Need a gif file name as args.\n");
		return -1;
	}
	FILE  *src_file = fopen(argv[1], "rb");
	FILE  *new_file = fopen("parser_output.gif", "wb");
	UINTN buffer_size = 0;
	UINTN file_size = 0;
	CHAR  *buffer = NULL;
	GIF   *gif = NULL;
	
	file_size = GetFileSizeByByte(src_file);

	if ( !GIFParserGetGifDataFromFile(src_file, &gif, &buffer_size) ) {
		return -1;
	}
	buffer = GIFParserGetDataBufferFromGif(gif, buffer_size);
	fwrite(buffer, buffer_size, 1, new_file);

	printf("file size: %d B\n", file_size);
	printf("buffer size: %d B\n", buffer_size);

	free(buffer);
	fclose(src_file);
	fclose(new_file);
	GIFParserClear(gif);

	return 0;
}
