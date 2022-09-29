#include <stdio.h>
#include <stdlib.h>
#include "gif_parser.h"

int main(int argc, const char** argv) {
	if (argc < 2) {
		printf("Need a gif file name as args.\n");
		return -1;
	}
	const CHAR  *src_file = argv[1];
	UINTN buffer_size = 0;
	FILE  *new_file = fopen("parser_output.gif", "wb");
	CHAR  *buffer = NULL;
	GIF   *gif = NULL;
	
	if ( !GIFParserGetGifDataFromFile(src_file, &gif, &buffer_size) ) {
		return -1;
	}
	buffer = GIFParserGetDataBufferFromGif(gif, buffer_size);
	fwrite(buffer, buffer_size, 1, new_file);

	printf("buffer size: %d B\n", buffer_size);

	free(buffer);
	fclose(new_file);
	GIFParserClear(gif);

	return 0;
}
