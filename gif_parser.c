/* This pragma will parse (*gif) picture created by gif_creator */
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
BOOL  GIFParserGetGifDataFromFile(IN FILE *fp, OUT GIF **gif, OUT UINTN *buffer_size);
CHAR  *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size);
CHAR  *GIFParserGetRawImageDataBufferFromFile(IN FILE *fp);
CHAR  *GIFParserGetRawImageDataBufferFromGif(IN GIF *gif);
BOOL  GIFParserClear(IN GIF *gif);

BOOL	GIFParserSetParserFromFile(IN FILE *fp, OUT GIF_PARSER *parser);
BOOL	GIFParserSetParserFromGif(IN GIF *gif, OUT GIF_PARSER *parser);
BOOL	GIFParserClearParser(OUT GIF_PARSER *parser);

// read memory and move file pointer 
BOOL GIFParserMemRead(OUT VOID *dst, IN VOID **src, IN UINT32 size); 

BOOL _HandleExtension(IN CHAR **src, IN CHAR label, OUT GIF **gif);
VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len);								 

// Just started
int main(int argc, const char** argv) {
	FILE  *src_file = fopen("test.gif", "rb");
	FILE  *new_file = fopen("parse_test.gif", "wb");
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
	printf("file size: %d B\n", buffer_size);

	free(buffer);
	fclose(src_file);
	fclose(new_file);
	GIFParserClear(gif);

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

BOOL GIFParserMemRead(OUT VOID *dst, IN VOID **src, IN UINT32 size) {
	VOID *buffer = memcpy(dst, *src, size);
	*src += size;
	if (buffer != NULL) return TRUE;
	return FALSE;
}

BOOL GIFParserGetGifDataFromFile(IN FILE *fp, OUT GIF **gif, OUT UINTN *buffer_size) {
	CHAR  *file_buffer = NULL;           // DO NOT FREE
	CHAR  *file_buffer_header = NULL;    // free this free file_buffer
	UINTN file_size = 0;
	UINT8 global_color_table_amount = 0;
	UINT8 local_color_table_amount = 0;
	BOOL  appExt_is_already = false;
	CHAR  c = 0;

	file_size = GetFileSizeByByte(fp);
	*buffer_size = file_size;
	(*gif) = (GIF*)malloc(sizeof(GIF));
	file_buffer = (CHAR *)malloc(sizeof(CHAR) * file_size);
	fread(file_buffer, file_size, 1, fp);
	file_buffer_header = file_buffer;

	if (fp == NULL || (*gif) == NULL || file_buffer == NULL) {
		printf("GIFParserGetGifDataFromFile: pointer error.\n");
		if (fp == NULL) {
			printf("[file pointer]\n");
		}
		else if ((*gif) == NULL) {
			printf("[gif pointer]\n");
		}
		else {
			printf("[file buffer]\n");
		}
		return FALSE;
	}
	
	(*gif)->GlobalColorTable = NULL;
	(*gif)->CommentExt = NULL;
	(*gif)->ImageData.frame_count = 0;
	(*gif)->ImageData.header = (GIF_IMG_DATA_NODE *)malloc(sizeof(GIF_IMG_DATA_NODE));
	(*gif)->ImageData.header->next = NULL;

	// Header Block 6 bytes
	GIFParserMemRead(&(*gif)->Header, (VOID**)&file_buffer, 6);

	if ( strncmp((*gif)->Header.gif_signature, "GIF", 3) != 0 ) {
		printf("GIFParserGetGifDataFromFile: this is not GIF file.\n");
		return FALSE;
	}

	if ( strncmp((*gif)->Header.gif_version, "89a", 3) != 0 ) {
		printf("GIFParserGetGifDataFromFile: GIF version not supported, use version \"89a\".\n");
		return FALSE;
	}

	// Logical Screen Descriptor
	GIFParserMemRead(&(*gif)->LogicalScreenDescriptor, (VOID**)&file_buffer, 7);
	
	// Global color table
	(*gif)->GlobalColorTable = NULL;
	if ( (*gif)->LogicalScreenDescriptor.flag_color_table == 1 ) {
		global_color_table_amount = 1 << ((*gif)->LogicalScreenDescriptor.flag_table_size + 1);
		(*gif)->GlobalColorTable = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * global_color_table_amount);
		GIFParserMemRead((*gif)->GlobalColorTable, (VOID**)&file_buffer, sizeof(GIF_COLOR_TABLE) * global_color_table_amount);
	}

	// Extension
	for ( ; ; ) {
		GIFParserMemRead(&c, (VOID**)&file_buffer, 1);
		
		// static UINTN total = 0;
		// ++total;
		// printf("%02X ", c);
		// if (total % 16 == 0) printf("\n");

		// 0x3B
		if (c == ';') {
			(*gif)->trailer = 0x3B;
			goto done;
		}

		// 0x21
		if (c == '!') {
			GIFParserMemRead(&c, (VOID**)&file_buffer, 1);
			if (!appExt_is_already && (c == 0xff) ) {  // AppExt
				appExt_is_already = TRUE;
			}
			
			if (appExt_is_already) {
				_HandleExtension(&file_buffer, c, gif);
			}
			else {
				*buffer_size -= 2;
			}
			continue;
		}

		*buffer_size -= 1;

		/*
		// 0x2C
		if (c != ',') {
			continue;
		}
		*/
	}

done:
	free(file_buffer_header);
	fclose(fp);

	return TRUE;
}

BOOL _HandleExtension(IN CHAR **src, IN CHAR label, OUT GIF **gif) {
	UINT8 size = 0;
	GIF_DATA_SUB_BLOCK_NODE *p = NULL;
	GIF_IMG_DATA_NODE *q = NULL;
	*src -= 2;      // 0x??21 already read

	switch (label) {
	case 0x01:          // Plain Text Extension 
		break;

	case 0xFF:          // Application Extension  19bytes
		GIFParserMemRead(&(*gif)->ApplicationExt, (VOID**)src, 19);
		return TRUE;

	case 0xFE:          // Comment Extension 
	{
		(*gif)->CommentExt = (GIF_COMMENT_EXTENSION*)malloc(sizeof(GIF_COMMENT_EXTENSION));
		(*gif)->CommentExt->data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
		(*gif)->CommentExt->data_sub_block_buffer.header->next = NULL;
		(*gif)->CommentExt->data_sub_block_buffer.block_count = 0;
		p = (*gif)->CommentExt->data_sub_block_buffer.header;
		GIFParserMemRead(&(*gif)->CommentExt->header, (VOID**)src ,2);
		GIFParserMemRead(&size, (VOID**)src ,1);
		while (size > 0) {
			GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
			new_node->next = NULL;
			new_node->data_size = size;
			GIFParserMemRead(new_node->data, (VOID**)src ,size);
			p->next = new_node;
			p = p->next;
			GIFParserMemRead(&size, (VOID**)src ,1);
			++( (*gif)->CommentExt->data_sub_block_buffer.block_count );
		}

		if (size == 0) {
			(*gif)->CommentExt->terminator = 0;
		}
		else {
			printf("HandleExtension: Comment Extension Error.\n");
			return FALSE;
		}
		return TRUE;
	}

	case 0xf9:          // Graphic Control Extension 
	{
		GIF_IMG_DATA_NODE *new_node_img = (GIF_IMG_DATA_NODE *)malloc(sizeof(GIF_IMG_DATA_NODE));  // one frame
		new_node_img->next = NULL;
		new_node_img->local_color_table = NULL;
		new_node_img->one_frame_data.data_sub_block_buffer.block_count = 0;
		new_node_img->one_frame_data.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
		new_node_img->one_frame_data.data_sub_block_buffer.header->next = NULL;
		p = new_node_img->one_frame_data.data_sub_block_buffer.header;

		GIFParserMemRead(&new_node_img->graphics_control_ext, (VOID**)src ,8);  	// Graphics control extension 
		GIFParserMemRead(&new_node_img->image_descriptor, (VOID**)src ,10);       // Image descriptor
		if ( new_node_img->image_descriptor.flag_color_table == 1 ) {    					// local color table
			UINT8 local_color_table_amount = 1 << (new_node_img->image_descriptor.flag_table_size + 1);
			new_node_img->local_color_table = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * local_color_table_amount );
			GIFParserMemRead(new_node_img->local_color_table, (VOID**)src ,sizeof(GIF_COLOR_TABLE) * local_color_table_amount);
		}
		GIFParserMemRead(&new_node_img->one_frame_data.LZW_Minimum_Code, (VOID**)src ,1);  // one frame data: LZW_Minimum_Code
		GIFParserMemRead(&size, (VOID**)src ,1);																			     // size is data_size
		while (size > 0) {																												   	     // one frame data: data_sub_block_buffer
			GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
			new_node->next = NULL;
			new_node->data_size = size;
			GIFParserMemRead(new_node->data, (VOID**)src, size);
			p->next = new_node;
			p = p->next;
			GIFParserMemRead(&size, (VOID**)src ,1);
			++( new_node_img->one_frame_data.data_sub_block_buffer.block_count );
		}

		if (size == 0) {  // one frame data: terminator, the last size is terminator = 0
			new_node_img->one_frame_data.terminator = 0;
			
			q = (*gif)->ImageData.header;  // Add one frame
			while (q->next != NULL) {
				q = q->next;
			}
			q->next = new_node_img;
			++((*gif)->ImageData.frame_count); // Added one frame node
		}
		else {
			printf("HandleExtension: Graphics Control Extension Error.\n");
			return FALSE;
		}
		return TRUE;

	}
	default:
		break;
	}
}

BOOL GIFParserClear(IN GIF *gif) {
	printf("Now function: GIFParserClear\n");
	if (gif->ImageData.header) {
		GIF_IMG_DATA_NODE *b, *d;
		GIF_DATA_SUB_BLOCK_NODE *p, *q;
		b = gif->ImageData.header;
		d = gif->ImageData.header->next;
		p = gif->ImageData.header->one_frame_data.data_sub_block_buffer.header;
		q = gif->ImageData.header->one_frame_data.data_sub_block_buffer.header->next;

		for (int i = 0; i < gif->ImageData.frame_count; ++i ) {
			for (int j = 0; j < b->one_frame_data.data_sub_block_buffer.block_count; ++j) {
				free(p);
				p = q;
				q = q->next;
			}
			free(b->local_color_table);
			free(b);
			b = d;
			d = d->next;
		}
	}

	if (gif->CommentExt) {
		GIF_DATA_SUB_BLOCK_NODE *p, *q;
		p = gif->CommentExt->data_sub_block_buffer.header;
		q = gif->CommentExt->data_sub_block_buffer.header->next;
		for (int i = 0; i < gif->CommentExt->data_sub_block_buffer.block_count; ++i) {
			free(p);
			p = q;
			q = q->next;
		}
		free(gif->CommentExt);
	}

	if (gif->GlobalColorTable) free(gif->GlobalColorTable);
	if (gif) free(gif);
	return TRUE;
}

CHAR  *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size) {
	CHAR *buffer = (CHAR *)malloc(sizeof(CHAR) * buffer_size);
	UINTN index = 0;
	UINTN size = 0;
	printf("Now function: GIFParserGetDataBufferFromGif\n");

	size = 13;
	memcpy(buffer+index, gif, size);
	index += size;

	if (gif->GlobalColorTable) {
		size = 1 << ( gif->LogicalScreenDescriptor.flag_table_size + 1 );
		memcpy(buffer+index, gif->GlobalColorTable, size*3);
		index += size*3;
	}

	size = 19;
	memcpy(buffer+index, &gif->ApplicationExt.header.introducer, size);
	index += size;

	if (gif->CommentExt) {
		memcpy(buffer+index, &gif->CommentExt->header.introducer, 2);
		index += 2;
		
		GIF_DATA_SUB_BLOCK_NODE *p = gif->CommentExt->data_sub_block_buffer.header->next;
		while (p != NULL) {
			size = 0;
			size += (p->data_size + 1);
			memcpy(buffer+index, &p->data_size, size);
			index += size;
			p = p->next;
		}

		// 0x00
		memcpy(buffer+index, &gif->CommentExt->terminator, 1);
		index += 1;
	}

	if (gif->ImageData.header) {
		GIF_IMG_DATA_NODE *q = gif->ImageData.header->next;
		while (q != NULL) {
			memcpy(buffer+index, &q->graphics_control_ext.header.introducer, 18);
			index += 18;

			if (q->local_color_table) {
				size = 0;
				size = 1 << (q->image_descriptor.flag_table_size + 1);
				memcpy(buffer+index, q->local_color_table, size*3);
				index += size*3;
			}

			memcpy(buffer+index, &q->one_frame_data.LZW_Minimum_Code, 1);
			index += 1;
			GIF_DATA_SUB_BLOCK_NODE *m = q->one_frame_data.data_sub_block_buffer.header->next;
			while (m != NULL) {
				size = 0;
				size += (m->data_size + 1);
				memcpy(buffer+index, &m->data_size, size);
				index += size;
				m = m->next;
			}
			memcpy(buffer+index, &q->one_frame_data.terminator, 1);
			index += 1;

			q = q->next;
		}
	}

	// 0x3B
	memcpy(buffer+index, &gif->trailer, 1);
	index += 1;

	printf("GIFParserGetDataBufferFromGif: index = %d\n", index);
	return buffer;
}

VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len) {
	FILE *fp = fopen("log", "ab");
	for (int i = 0; i < len; ++i) {
		fwrite(buffer+i, 1, 1, fp);
	}
	fclose(fp);
}

