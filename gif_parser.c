/* This pragma will parse gif picture created by gif_creator */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <lzw/lzw.h>
#include "gif_parser.h"

#define IN 
#define OUT 
#define TRUE true
#define FALSE false

typedef long  UINTN;
typedef void  VOID;
typedef bool BOOL;

UINTN GetFileSizeByByte(IN FILE *fp);
BOOL  GIFParserGetGifDataFromFile(IN FILE *fp, OUT GIF *gif);
CHAR  *GIFParserGetDataBufferFromGif(IN GIF *gif);
CHAR  *GIFParserGetRawImageDataBufferFromFile(IN FILE *fp);
CHAR  *GIFParserGetRawImageDataBufferFromGif(IN GIF *gif);
BOOL  GIFParserClear(IN GIF *gif);

BOOL	GIFParserSetParserFromFile(IN FILE *fp, OUT GIF_PARSER *parser);
BOOL	GIFParserSetParserFromGif(IN GIF *gif, OUT GIF_PARSER *parser);
BOOL	GIFParserClearParser(OUT GIF_PARSER *parser);

VOID *GIFParserMemCopy(VOID *dst, const VOID *src, UINT32 size);

// Just started
int main(int argc, const char** argv) {
	FILE  *src_file = fopen("test.gif", "rb");
	FILE  *new_file = fopen("parse_test.gif", "wb");
	UINTN file_size = 0;
	CHAR  *buffer = NULL;
	GIF   *gif = NULL;
	
	file_size = GetFileSizeByByte(src_file);
	gif = GIFParserGetGifDataFromFile(src_file);
	buffer = GIFParserGetDataBufferFromGif(gif);
	fwrite(buffer, file_size, 1, new_file);

	GIFParserClear(gif);
	free(buffer);
	fclose(src_file);
	fclose(new_file);

	printf("file size: %d B\n", file_size);


	fclose(src_file);
	return 0;
}





UINTN GetFileSizeByByte(IN FILE *fp) {
	UINTN file_size = 0;
	if ( fp == NULL ) {
		printf("GetFileSizeByByte: fp is NULL.\n");
	}

	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	rewind(fp);

	return file_size;
}

VOID *GIFParserMemCopy(VOID *dst, const VOID *src, UINT32 size) {
	return memcpy(dst, src, size);
}

BOOL GIFParserGetGifDataFromFile(IN FILE *fp, OUT GIF *gif) {
	GIF *gif = NULL;
	CHAR *file_buffer = NULL;
	UINTN file_size = 0;
	UINTN index = 0;
	UINT8 global_color_table_amount = 0;
	UINT8 local_color_table_amount = 0;

	file_size = GetFileSizeByByte(fp);
	gif = (GIF*)malloc(sizeof(GIF));
	fread(file_buffer, file_size, 1, fp);

	if (fp == NULL || gif == NULL || file_buffer == NULL) {
		prints("GIFParserGetGifDataFromFile: pointer error.\n");
		return FALSE;
	}
	
	// Header Block 6 bytes
	GIFParserMemCopy(&gif->Header, file_buffer, 6);
	index += 6;

	if ( strncmp(gif->Header.gif_signature, "GIF", 3) != 0 ) {
		printf("GIFParserGetGifDataFromFile: this is not GIF file.\n");
		return FALSE;
	}

	if ( strncmp(gif->Header.gif_version, "89a", 3) != 0 ) {
		printf("GIFParserGetGifDataFromFile: GIF version not supported, use version \"89a\".\n");
		return FALSE;
	}

	// Logical Screen Descriptor
	GIFParserMemCopy(&gif->LogicalScreenDescriptor, file_buffer+index, 7);
	index += 7;
	
	// Global color table
	if ( gif->LogicalScreenDescriptor.flag_color_table ) {
		global_color_table_amount = (2 << gif->LogicalScreenDescriptor.flag_table_size);
		gif->GlobalColorTable = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * global_color_table_amount);
		GIFParserMemCopy(gif->GlobalColorTable, file_buffer+index, sizeof(GIF_COLOR_TABLE) * global_color_table_amount);
		index += sizeof(GIF_COLOR_TABLE) * global_color_table_amount;
	}
	else {
		gif->GlobalColorTable = NULL;
	}

	// Extension

	return TRUE;
}

BOOL GIFParserClear(IN GIF *gif) {
	free(gif->GlobalColorTable);
	free(gif);
	return TRUE;
}

