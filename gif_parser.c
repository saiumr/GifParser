/* This pragma will parse (*gif) picture created by gif_creator */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lzw/lzw.h>
#include "gif_parser.h"

#define ALLOC_COMPONENT_AMOUNT 512
#define ALLOC_COMPONENT_SIZE   ALLOC_COMPONENT_AMOUNT*sizeof(GIF_COMPONENT)
UINT16  gAllocComponentCount = 1;

// read memory and move file pointer 
BOOL _GIFParserMemRead(OUT VOID *dst, IN VOID **src, IN UINT32 size); 

BOOL _HandleExtension(IN CHAR **src, IN CHAR label, OUT GIF **gif);
BOOL _HandleImageData(IN CHAR **src, OUT GIF **gif);

VOID _RecordComponentOrder(IN GIF_COMPONENT key, OUT GIF **gif);

CHAR *_GIFParserGetRawImageDataBufferFromFile(IN FILE *fp);
CHAR *_GIFParserGetRawImageDataBufferFromGif(IN GIF *gif);

VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len);								 

typedef struct {
	GIF_APP_EXT_DATA 				*app;
	GIF_COMMENT_EXT_DATA		*comment;
	GIF_GRAPHICS_EXT_DATA   *graphics;
	GIF_IMAGE_DATA          *image;
} _GIF_TAILER_POINTER;

_GIF_TAILER_POINTER gTailerPointer;


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

BOOL GIFParserGetGifDataFromFile(IN const CHAR *filename, OUT GIF **gif, OUT UINTN *buffer_size) {
	printf("Now Function: GIFParserGetGifDataFromFile\n");
	CHAR   *file_buffer = NULL;           // DO NOT FREE
	CHAR   *file_buffer_header = NULL;    // free this free file_buffer
	UINTN  file_size = 0;
	CHAR   c = 0;
	FILE *src = fopen(filename, "rb");
	
	file_size = GetFileSizeByByte(src);
	*buffer_size = file_size;
	file_buffer = (CHAR *)malloc(sizeof(CHAR) * file_size);
	fread(file_buffer, file_size, 1, src);
	file_buffer_header = file_buffer;
	(*gif) = (GIF*)malloc(sizeof(GIF));

	if (src == NULL || (*gif) == NULL || file_buffer == NULL) {
		printf("GIFParserGetGifDataFromFile: pointer error.\n");
		if (src == NULL) {
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
	
	// Init
	(*gif)->GlobalColorTable = NULL;
	(*gif)->AppExtHeader = (GIF_APP_EXT_DATA *)malloc(sizeof(GIF_APP_EXT_DATA));
	(*gif)->AppExtHeader->next = NULL;
	gTailerPointer.app = (*gif)->AppExtHeader;

	(*gif)->CommentExtHeader = (GIF_COMMENT_EXT_DATA *)malloc(sizeof(GIF_COMMENT_EXT_DATA));
	(*gif)->CommentExtHeader->next = NULL;
	gTailerPointer.comment = (*gif)->CommentExtHeader;

	(*gif)->GraphicsExtHeader = (GIF_GRAPHICS_EXT_DATA *)malloc(sizeof(GIF_GRAPHICS_CONTROL_EXTENSION));
	(*gif)->GraphicsExtHeader->next = NULL;
	gTailerPointer.graphics = (*gif)->GraphicsExtHeader;

	(*gif)->ImageDataHeader = (GIF_IMAGE_DATA *)malloc(sizeof(GIF_IMAGE_DATA));
	(*gif)->ImageDataHeader->next = NULL;
	gTailerPointer.image = (*gif)->ImageDataHeader;
	(*gif)->ImageDataHeader->local_color_table = NULL;

	(*gif)->ComponentOrder.component = (GIF_COMPONENT *)malloc(gAllocComponentCount*ALLOC_COMPONENT_SIZE);
	++gAllocComponentCount;
	(*gif)->ComponentOrder.size = 0;

	// Header Block 6 bytes
	_GIFParserMemRead(&(*gif)->Header, (VOID**)&file_buffer, 6);

	if ( strncmp((*gif)->Header.gif_signature, "GIF", 3) != 0 ) {
		printf("GIFParserGetGifDataFromFile: this is not GIF file.\n");
		return FALSE;
	}

	if ( strncmp((*gif)->Header.gif_version, "89a", 3) != 0 ) {
		printf("GIFParserGetGifDataFromFile: GIF version not supported, use version \"89a\".\n");
		return FALSE;
	}

	// Logical Screen Descriptor 7 Bytes
	_GIFParserMemRead(&(*gif)->LogicalScreenDescriptor, (VOID**)&file_buffer, 7);
	
	// Global color table
	(*gif)->GlobalColorTable = NULL;
	if ( (*gif)->LogicalScreenDescriptor.flag_color_table == 1 ) {
		UINT16 global_color_table_amount = 1 << ((*gif)->LogicalScreenDescriptor.flag_table_size + 1);
		(*gif)->GlobalColorTable = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * global_color_table_amount);
		_GIFParserMemRead((*gif)->GlobalColorTable, (VOID**)&file_buffer, sizeof(GIF_COLOR_TABLE) * global_color_table_amount);
	}

	// Extensions and Image Data
	for ( ; ; ) {
		_GIFParserMemRead(&c, (VOID**)&file_buffer, 1);

		// 0x3B
		if (c == ';') {
			(*gif)->trailer = 0x3B;
			goto done;
		}

		// 0x21
		if (c == '!') {
			_GIFParserMemRead(&c, (VOID**)&file_buffer, 1);
			_HandleExtension(&file_buffer, c, gif);
			continue;
		}

		// 0x2C
		if (c != ',') {
			continue;
		}

		// Image Descriptor
		_HandleImageData(&file_buffer, gif);

	}

done:
	free(file_buffer_header);
	fclose(src);

	return TRUE;
}

CHAR *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size) {
	printf("Now function: GIFParserGetDataBufferFromGif\n");
	CHAR *buffer = (CHAR *)malloc(sizeof(CHAR) * buffer_size);
	UINTN index = 0;
	UINTN size = 0;

  // header && logical screen descriptor
	size = 13;
	memcpy(buffer+index, gif, size);
	index += size;

	// Global color table
	if (gif->GlobalColorTable) {
		size = 1 << ( gif->LogicalScreenDescriptor.flag_table_size + 1 );
		memcpy(buffer+index, gif->GlobalColorTable, size * 3);
		index += size * 3;
	}

	// extension and image data
	GIF_APP_EXT_DATA 				*a = gif->AppExtHeader->next;
	GIF_COMMENT_EXT_DATA		*c = gif->CommentExtHeader->next;
	GIF_GRAPHICS_EXT_DATA   *g = gif->GraphicsExtHeader->next;
	GIF_IMAGE_DATA          *i = gif->ImageDataHeader->next;
	for (UINTN component_index = 0; component_index < gif->ComponentOrder.size; ++component_index) {
		switch (gif->ComponentOrder.component[component_index]) {
			case kAppExt:
			{
				if (a != NULL) {
					memcpy(buffer+index, &a->app.header.introducer, 14);
					index += 14;

					GIF_DATA_SUB_BLOCK_NODE *p = a->app.data_sub_block_buffer.header->next;
					while (p != NULL) {
						memcpy(buffer+index, &p->data_size, 1);
						index += 1;

						size = p->data_size;
						memcpy(buffer+index, p->data, size);
						index += size;

						p = p->next;
					}

					memcpy(buffer+index, &a->app.terminator, 1);
					index += 1;
					a = a->next;
				}
				break;
			}
			case kCommentExt:
			{
				if (c != NULL) {
					memcpy(buffer+index, &c->comment.header.introducer, 2);
					index += 2;

					GIF_DATA_SUB_BLOCK_NODE *p = c->comment.data_sub_block_buffer.header->next;
					while (p != NULL) {
						memcpy(buffer+index, &p->data_size, 1);
						index += 1;

						size = p->data_size;
						memcpy(buffer+index, p->data, size);
						index += size;

						p = p->next;
					}

					memcpy(buffer+index, &c->comment.terminator, 1);
					index += 1;
					c = c->next;
				}
				break;
			} 
			case kGraphicsExt:
			{
				if (g != NULL) {
					memcpy(buffer+index, &g->graphics.header.introducer, 8);
					index += 8;
					g = g->next;
				}
				break;
			}
			case kImageData:
			{
				if (i != NULL) {
					memcpy(buffer+index, &i->image_descriptor, 10);
					index += 10;

					if (i->local_color_table) {
						size = 1 << (i->image_descriptor.flag_table_size + 1);
						memcpy(buffer+index, i->local_color_table, size * 3);
						index += size * 3;
					}

					memcpy(buffer+index, &i->one_frame_data.LZW_Minimum_Code, 1);
					index += 1;

					GIF_DATA_SUB_BLOCK_NODE *p = i->one_frame_data.data_sub_block_buffer.header->next;
					while (p != NULL) {
						memcpy(buffer+index, &p->data_size, 1);
						index += 1;

						size = p->data_size;
						memcpy(buffer+index, p->data, size);
						index += size;
						p = p->next;
					}
					memcpy(buffer+index, &i->one_frame_data.terminator, 1);
					index += 1;

					i = i->next;
				}
				break;
			}
			default:
				break;
		}
	}

	// 0x3B
	memcpy(buffer+index, &gif->trailer, 1);
	index += 1;

	printf("GIFParserGetDataBufferFromGif: index = %d\n", index);
	return buffer;
}

BOOL GIFParserClear(IN GIF *gif) {
	printf("Now function: GIFParserClear\n");
	if (gif->ImageDataHeader) {
		GIF_IMAGE_DATA *b = gif->ImageDataHeader;
		GIF_IMAGE_DATA *d = b->next;
		
		while (b != NULL) {
			GIF_DATA_SUB_BLOCK_NODE *p = b->one_frame_data.data_sub_block_buffer.header;
			GIF_DATA_SUB_BLOCK_NODE *q = p->next;

			while (p != NULL) {
				free(p);
				p = q;
				q = q->next;
			}

			if (b->local_color_table) {
				free(b->local_color_table);
			}

			free(b);
			b = d;
			d = d->next;
		}
	}

	if (gif->CommentExtHeader) {
		GIF_COMMENT_EXT_DATA *r = gif->CommentExtHeader;
		GIF_COMMENT_EXT_DATA *s = r->next;

		while (r != NULL) {
			GIF_DATA_SUB_BLOCK_NODE *p = r->comment.data_sub_block_buffer.header;
			GIF_DATA_SUB_BLOCK_NODE *q = p->next;

			while (p != NULL) {
				free(p);
				p = q;
				q = q->next;
			}

			free(r);
			r = s;
			s = s->next;
		} 
	}

	if (gif->AppExtHeader) {
		GIF_APP_EXT_DATA *a = gif->AppExtHeader;
		GIF_APP_EXT_DATA *s = a->next;

		while (a != NULL) {
			GIF_DATA_SUB_BLOCK_NODE *p = a->app.data_sub_block_buffer.header;
			GIF_DATA_SUB_BLOCK_NODE *q = p->next;

			while (p != NULL) {
				free(p);
				p = q;
				q = q->next;
			}

			free(a);
			a = s;
			s = s->next;
		} 
	}

	if (gif->GraphicsExtHeader) {
		GIF_APP_EXT_DATA *g = gif->AppExtHeader;
		GIF_APP_EXT_DATA *h = g->next;
		while (g) {
			free(g);
			g = h;
			h = h->next;
		}
	}

	if (gif->ComponentOrder.component) {
		free(gif->ComponentOrder.component);
	}

	if (gif->GlobalColorTable) free(gif->GlobalColorTable);
	if (gif) free(gif);
	return TRUE;
}

BOOL _GIFParserMemRead(OUT VOID *dst, IN VOID **src, IN UINT32 size) {
	VOID *buffer = memcpy(dst, *src, size);
	*src += size;
	if (buffer != NULL) return TRUE;
	return FALSE;
}

BOOL _HandleExtension(IN CHAR **src, IN CHAR label, OUT GIF **gif) {
	UINT8 size = 0;

	switch (label) {
	case 0x01:           // Plain Text Extension 
		break;

	case 0xFF:          // Application Extension  19bytes
	{
		if (gTailerPointer.app == NULL || gTailerPointer.app->next != NULL) {
			printf("gTailerPointer.app error\n");
			return FALSE;
		}

		GIF_APP_EXT_DATA *new_app_node = (GIF_APP_EXT_DATA *)malloc(sizeof(GIF_APP_EXT_DATA));
		new_app_node->next = NULL;
		new_app_node->app.data_sub_block_buffer.block_count = 0;
		new_app_node->app.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
		GIF_DATA_SUB_BLOCK_NODE *p = new_app_node->app.data_sub_block_buffer.header;
		p->next = NULL;

		new_app_node->app.header.introducer = 0x21;
		new_app_node->app.header.label      = 0xFF;
		_GIFParserMemRead(&new_app_node->app.size, (VOID**)src, 12);
		_GIFParserMemRead(&size, (VOID**)src, 1);

		while (size > 0) {
			GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
			new_node->data_size = size;
			_GIFParserMemRead(new_node->data, (VOID**)src, size);
			_GIFParserMemRead(&size, (VOID**)src, 1);
			p->next = new_node;
			p = p->next;
			++(new_app_node->app.data_sub_block_buffer.block_count);
		}
		
		if (size == 0) {
			new_app_node->app.terminator = 0;	
			gTailerPointer.app->next = new_app_node;
			gTailerPointer.app = new_app_node;
			_RecordComponentOrder(kAppExt, gif);
		}
		else {
			printf("_HandleExtension[0xFF]: Load Error.\n");
			return FALSE;
		}
		return TRUE;
	}

	case 0xFE:          // Comment Extension 
	{
		if (gTailerPointer.comment == NULL || gTailerPointer.comment->next != NULL) {
			printf("gTailerPointer.comment error\n");
			return FALSE;
		}
		GIF_COMMENT_EXT_DATA *new_com_node = (GIF_COMMENT_EXT_DATA *)malloc(sizeof(GIF_COMMENT_EXT_DATA));
		new_com_node->next = NULL;
		new_com_node->comment.data_sub_block_buffer.block_count = 0;
		new_com_node->comment.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
		GIF_DATA_SUB_BLOCK_NODE *p = new_com_node->comment.data_sub_block_buffer.header;
		p->next = NULL;

		new_com_node->comment.header.introducer = 0x21;
		new_com_node->comment.header.label      = 0xFE;
		_GIFParserMemRead(&size, (VOID**)src, 1);
		while (size > 0) {
			GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
			new_node->data_size = size;
			_GIFParserMemRead(new_node->data, (VOID**)src, size);
			_GIFParserMemRead(&size, (VOID**)src, 1);
			p->next = new_node;
			p = p->next;
			++(new_com_node->comment.data_sub_block_buffer.block_count);
		}
		
		if (size == 0) {
			new_com_node->comment.terminator = 0;	
			gTailerPointer.comment->next = new_com_node;
			gTailerPointer.comment = new_com_node;
			_RecordComponentOrder(kCommentExt, gif);
		}
		else {
			printf("_HandleExtension[0xFE]: Load Error.\n");
			return FALSE;
		}
		return TRUE;
	}

	case 0xF9:          // Graphic Control Extension 
	{
		if (gTailerPointer.graphics == NULL || gTailerPointer.graphics->next != NULL) {
			printf("gTailerPointer.graphics error\n");
			return FALSE;
		}
		
		GIF_GRAPHICS_EXT_DATA *new_graphics_node = (GIF_GRAPHICS_EXT_DATA *)malloc(sizeof(GIF_GRAPHICS_EXT_DATA));  // one frame
		new_graphics_node->next = NULL;
		new_graphics_node->graphics.header.introducer = 0x21;
		new_graphics_node->graphics.header.label      = 0xF9;
		_GIFParserMemRead(&new_graphics_node->graphics.size, (VOID**)src, 6);  	// Graphics control extension 
		gTailerPointer.graphics->next = new_graphics_node;
		gTailerPointer.graphics = new_graphics_node;
		_RecordComponentOrder(kGraphicsExt, gif);

		return TRUE;
	}

	default:
		break;
	}
}

BOOL _HandleImageData(IN CHAR **src, OUT GIF **gif) {
 	UINTN size = 0;
	*src -= 1;  // already read ','  -> 0x2C

	if (gTailerPointer.image == NULL || gTailerPointer.image->next != NULL) {
		printf("gTailerPointer.image error\n");
		return FALSE;
	}

	GIF_IMAGE_DATA *new_image_node = (GIF_IMAGE_DATA *)malloc(sizeof(GIF_IMAGE_DATA));
	new_image_node->next = NULL;
	new_image_node->local_color_table = NULL;
	new_image_node->one_frame_data.data_sub_block_buffer.block_count = 0;
	new_image_node->one_frame_data.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
	GIF_DATA_SUB_BLOCK_NODE *p = new_image_node->one_frame_data.data_sub_block_buffer.header;
	p->next = NULL;

	_GIFParserMemRead(&new_image_node->image_descriptor, (VOID**)src, 10);       // Image descriptor
	if ( new_image_node->image_descriptor.flag_color_table == 1 ) {    					// local color table
		UINT16 local_color_table_amount = 1 << (new_image_node->image_descriptor.flag_table_size + 1);
		new_image_node->local_color_table = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * local_color_table_amount );
		_GIFParserMemRead(new_image_node->local_color_table, (VOID**)src, sizeof(GIF_COLOR_TABLE) * local_color_table_amount);
	}
	_GIFParserMemRead(&new_image_node->one_frame_data.LZW_Minimum_Code, (VOID**)src, 1);  // one frame data: LZW_Minimum_Code
	_GIFParserMemRead(&size, (VOID**)src, 1);																			     // size is data_size
	while (size > 0) {																												   	     // one frame data: data_sub_block_buffer
		GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
		new_node->next = NULL;
		new_node->data_size = size;
		_GIFParserMemRead(new_node->data, (VOID**)src, size);
		p->next = new_node;
		p = p->next;
		_GIFParserMemRead(&size, (VOID**)src, 1);
		++(new_image_node->one_frame_data.data_sub_block_buffer.block_count);
	}

	if (size == 0) {  // one frame data: terminator, the last size is terminator = 0
		new_image_node->one_frame_data.terminator = 0;		
		gTailerPointer.image->next = new_image_node;
		gTailerPointer.image = new_image_node;
		_RecordComponentOrder(kImageData, gif);
	}
	else {
		printf("_HandleImageData: Load Image Data Error.\n");
		return FALSE;
	}

	return TRUE;
}

VOID _RecordComponentOrder(IN GIF_COMPONENT key, OUT GIF **gif) {
	if (gAllocComponentCount*ALLOC_COMPONENT_AMOUNT - (*gif)->ComponentOrder.size < 10) {
		++gAllocComponentCount;
		(*gif)->ComponentOrder.component = (GIF_COMPONENT *)realloc((*gif)->ComponentOrder.component, gAllocComponentCount * ALLOC_COMPONENT_SIZE);
	}
	(*gif)->ComponentOrder.component[(*gif)->ComponentOrder.size] = key;
	++((*gif)->ComponentOrder.size);
}

VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len) {
	FILE *fp = fopen("log", "ab");
	for (int i = 0; i < len; ++i) {
		fwrite(buffer+i, 1, 1, fp);
	}
	fclose(fp);
}

