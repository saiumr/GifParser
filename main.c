#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gif_parser.h"

int main(int argc, const char** argv) {
	if (argc < 2) {
		printf("Need a gif file name as args.\n");
		return -1;
	}
	const CHAR  *src_file = argv[1];
	CHAR  *frame_file = (CHAR *)argv[1];
	UINTN buffer_size = 0;
	FILE  *new_file = fopen("parser_output.gif", "wb");
	UINT8 *buffer = NULL;
	GIF   *gif = NULL;
	
	if ( !GIFParserGetGifDataFromFile(src_file, &gif, &buffer_size) ) {
		return -1;
	}
	buffer = GIFParserGetDataBufferFromGif(gif, buffer_size);
	fwrite(buffer, buffer_size, 1, new_file);

	printf("buffer size: %d B\n", buffer_size);

	IMG_ANIMATION *animation = NULL;
	if ( !GIFParserGetAnimationFromFile(src_file, &animation) ) {
		goto done;
	}
	UINT8 **frame_buffer = (UINT8 **)malloc(animation->count * sizeof(UINT8 **));
	if (frame_buffer == NULL) {
		goto done;
	}

	CHAR file_name[32] = {0};
	CHAR *file_name_ptr = frame_file;
	while (*file_name_ptr != '\0') {
		++file_name_ptr;
	}
	// jump ".gif"
	file_name_ptr -= 5;
	while (*file_name_ptr != '\\') {
		--file_name_ptr;
	}
	// start at file name
	file_name_ptr += 1;
	for (UINTN i = 0; i < 32; ++i) {
		if (*file_name_ptr == '.') {
			file_name[i] = '\0';
			break;
		}
		file_name[i] = *file_name_ptr;
		++file_name_ptr;
	}
	CHAR filepath[32] = "./frames/";
	CHAR str_count[16] = {0};
	CHAR file[32] = {0};
	for (UINTN i = 0; i < animation->count; ++i) {
		UINTN frame_size = 0;
		frame_buffer[i] = GIFParserAnimationFramesTransformBMP(animation, i, &frame_size);
		sprintf(str_count, "%u", i);
		strcat(str_count, ".bmp");
		if (i == 0) {
			strcat(filepath, file_name);
		}
		sprintf(file, "%s%s", filepath, str_count);
		FILE *fp = fopen(file, "wb");
		fwrite(frame_buffer[i], frame_size, 1, fp);
		fclose(fp);
	}


done:
	free(buffer);
	fclose(new_file);
	GIFParserClear(gif);
	if (frame_buffer) {
		for (UINTN i = 0; i < animation->count; ++i) {
			if (frame_buffer[i]) free(frame_buffer[i]);
		}
		free(frame_buffer);
	}
	GIFParserClearAnimation(animation);
	
	printf("- Done -\n");

	return 0;
}
