/* This pragma will parse (*gif) picture created by gif_creator */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lzw/lzw.h>
#include "gif_parser.h"

#define ALLOC_COMPONENT_AMOUNT 512
#define ALLOC_COMPONENT_SIZE   ALLOC_COMPONENT_AMOUNT*sizeof(GIF_COMPONENT)
UINT16  gAllocComponentCount = 1;


UINTN _GetFileSizeByByte(IN FILE *fp);

// read memory and move file pointer 
BOOL _GIFParserMemRead(OUT VOID *dst, IN VOID **src, IN UINT32 size); 

BOOL _HandleExtension(IN CHAR **src, IN CHAR label, OUT GIF **gif);
BOOL _HandleImageData(IN CHAR **src, OUT GIF **gif);

VOID _RecordComponentOrder(IN GIF_COMPONENT key, OUT GIF **gif);

VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len);								 

typedef struct {
	GIF_APP_EXT_DATA 				*app;
	GIF_COMMENT_EXT_DATA		*comment;
	GIF_GRAPHICS_EXT_DATA   *graphics;
	GIF_IMAGE_DATA          *image;
} _GIF_TAILER_POINTER;

_GIF_TAILER_POINTER gTailerPointer;

UINT8  *GIFParserAnimationFramesTransformBMP(IN IMG_ANIMATION *animation, IN UINTN frame_index, OUT UINTN *frame_size) {
	printf("Now function: GIFParserGetAnimationFrames [frame_index = %u]\n", frame_index);
	if (frame_index >= animation->count) {
		return NULL;
	}

	// one frame buffer
	UINTN line_data = animation->w * sizeof(IMG_FRAME);
	if (line_data % 4 != 0) {
		line_data = (line_data / 4 + 1) * 4;
	}
	UINTN line_diff = line_data - animation->w*3;

	UINTN buffer_color_amount = line_data * animation->h; // 1 * extension width data * h (colors)
	*frame_size = 54 + buffer_color_amount;
	UINT8 *buffer = (UINT8 *)malloc(*frame_size);
	printf("bmp file size: %u\n", *frame_size);
	if (buffer == NULL) {
		return NULL;
	}
	BMP_IMAGE_HEADER bmp_header;
	bmp_header.CharB = 'B';
	bmp_header.CharM = 'M';
	bmp_header.Size  = *frame_size;  // bit_size/8 -> Byte size
	bmp_header.Reserved[0] = 0;
	bmp_header.Reserved[1] = 0;
	bmp_header.ImageOffset = 0x36;
	bmp_header.InfoHeaderSize = 0x28;  // 40 Bytes
	bmp_header.PixelWidth  = animation->w;
	bmp_header.PixelHeight = animation->h;
	bmp_header.Planes = 1;
	bmp_header.BitPerPixel = 0x18;     // 24 bits per pixel
	bmp_header.CompressionType = 0;
	bmp_header.ImageSize = bmp_header.Size - bmp_header.ImageOffset;
	bmp_header.XPixelsPerMeter = 0x1625;
	bmp_header.YPixelsPerMeter = 0x1625;
	bmp_header.NumberOfColors  = 0;
	bmp_header.ImportantColors = 0;

	// header
	memcpy(buffer, &bmp_header, 54);
	// image data
	UINTN index = 54;
	for (UINTN row = animation->h - 1; row >= 0; --row) {
		for (UINTN col = 0; col < animation->w; ++col) {
			buffer[index++] = animation->frames[frame_index][animation->w * row + col].b;
			buffer[index++] = animation->frames[frame_index][animation->w * row + col].g;
			buffer[index++] = animation->frames[frame_index][animation->w * row + col].r;
		}
		if (line_diff != 0) {
			for (UINTN diff = 0; diff < line_diff; ++diff) {
				buffer[index++] = 0;
			}
		}
		// row is unsigned value
		if (row == 0) {
			break;
		}
	}

	return buffer;
}

BOOL	GIFParserGetAnimationFromFile(IN const CHAR *filename, OUT IMG_ANIMATION **animation) {
	printf("Now function: GIFParserGetAnimationFromFile\n");
	GIF   *gif = NULL;
	UINTN buffer_size = 0;
	if (!GIFParserGetGifDataFromFile(filename, &gif, &buffer_size)) {
		return FALSE;
	}

	if (!GIFParserGetAnimationFromGif(gif, animation)) {
		return FALSE;
	}

	GIFParserClear(gif);

	return TRUE;
}

BOOL	GIFParserGetAnimationFromGif(IN GIF *gif, OUT IMG_ANIMATION **animation) {
	printf("Now function: GIFParserGetAnimationFromGif\n");
	*animation = (IMG_ANIMATION *)malloc(sizeof(IMG_ANIMATION));
	if ( (*animation) == NULL ) {
		return FALSE;
	}

	(*animation)->w = gif->LogicalScreenDescriptor.canvas_width;   // true area
	(*animation)->h = gif->LogicalScreenDescriptor.canvas_height;	
	(*animation)->delays = gif->GraphicsExtHeader->next->graphics.delay_time * 10;  // ms
	(*animation)->count = gif->FramesCount;
	(*animation)->frames = (IMG_FRAME **)malloc(sizeof(IMG_FRAME*) * (*animation)->count);
	if ((*animation)->frames == NULL) {
		return FALSE;
	}

	printf("true width/height: %u, %u\n", (*animation)->w, (*animation)->h);

	typedef enum COLOR_KIND {
		LOCAL_COLOR,
		GLOBAL_COLOR
	} COLOR_KIND;
	UINTN pixel_count = (*animation)->w * (*animation)->h;
	UINT8 *color_index_list = (UINT8 *)malloc(sizeof(UINT8) * pixel_count);
	COLOR_KIND *color_index_kind = (COLOR_KIND *)malloc(sizeof(COLOR_KIND) * pixel_count);
	if (color_index_list == NULL) {
		return FALSE;
	}

	UINTN frame_count = 0;
	GIF_IMAGE_DATA  *prev_image = gif->ImageDataHeader;
	GIF_IMAGE_DATA  *image = prev_image->next;
	GIF_GRAPHICS_EXT_DATA *graphics = gif->GraphicsExtHeader->next;
	GIF_COLOR_TABLE transparency_color = {0, 0, 0};
	GIF_COLOR_TABLE bg_color = {255, 255, 255};

	if (gif->LogicalScreenDescriptor.flag_color_table == 1) {
			bg_color.r = gif->GlobalColorTable[gif->LogicalScreenDescriptor.bg_color_index].r;
			bg_color.g = gif->GlobalColorTable[gif->LogicalScreenDescriptor.bg_color_index].g;
			bg_color.b = gif->GlobalColorTable[gif->LogicalScreenDescriptor.bg_color_index].b;
	}

	while (image != NULL) {
		UINTN changed_data_size = image->image_descriptor.width * image->image_descriptor.height;  // exclude non-changed parts
		UINT8 *changed_color_index_list = (UINT8 *)malloc(changed_data_size);
		UINT8 *changed_data = (UINT8 *)malloc(sizeof(UINT8) * image->one_frame_data.data_sub_block_buffer.total_data_size);
		if (changed_data == NULL) {
			return FALSE;
		}

		GIF_DATA_SUB_BLOCK_NODE *p = image->one_frame_data.data_sub_block_buffer.header->next;
		UINTN changed_data_index = 0;
		while (p != NULL) {
			memcpy(changed_data + changed_data_index, p->data, p->data_size);
			changed_data_index += p->data_size;
			p = p->next;
		}

		UINT8 bit_size = image->one_frame_data.LZW_Minimum_Code;
		UINTN out_changed_data_size = 0;
		lzw_decompress(bit_size, changed_data_size, changed_data, &out_changed_data_size, &changed_color_index_list);
		printf("changed_data_index: %u, changed_data_size: %u, out_changed_data_size: %u\n", changed_data_index, changed_data_size, out_changed_data_size);

		// restore color
		IMG_FRAME *frame = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * pixel_count);
		if (frame == NULL) {
			return FALSE;
		}

		if (graphics->graphics.flag_transparency_used == 1) {	
			if (image->local_color_table) {
				transparency_color.r = image->local_color_table[graphics->graphics.transparent_color_index].r;
				transparency_color.g = image->local_color_table[graphics->graphics.transparent_color_index].g;
				transparency_color.b = image->local_color_table[graphics->graphics.transparent_color_index].b;
			}
			else {
				transparency_color.r = gif->GlobalColorTable[graphics->graphics.transparent_color_index].r;
				transparency_color.g = gif->GlobalColorTable[graphics->graphics.transparent_color_index].g;
				transparency_color.b = gif->GlobalColorTable[graphics->graphics.transparent_color_index].b;
			}
		}

		printf("frame count: [%u]\n", frame_count);
		printf("image data size = %u\n", sizeof(IMG_FRAME) * pixel_count);
		printf("w = %u, h = %u, delay = %u, left = %u, top = %u\n", image->image_descriptor.width, image->image_descriptor.height, (*animation)->delays, \
			image->image_descriptor.left, image->image_descriptor.top);
		if (graphics->graphics.flag_transparency_used == 1)
		printf("transparency_color: %u %u %u\n", transparency_color.r, transparency_color.g, transparency_color.b);
		if (gif->LogicalScreenDescriptor.flag_color_table == 1)
		printf("bg_color: %u %u %u\n", bg_color.r, bg_color.g, bg_color.b);

		// In gif every frame's image data depends on previous frame. change changed data, reserved out of changed data back ground color index flush;
		UINTN global_part_index = 0;
		for (UINTN row = 0; row < (*animation)->h; ++row) {
			for (UINTN col = 0; col < (*animation)->w; ++col) {
				if ( col < image->image_descriptor.left || row < image->image_descriptor.top || \
				  col > image->image_descriptor.left + image->image_descriptor.width - 1 || \
					row > image->image_descriptor.top + image->image_descriptor.height - 1 ) {
					switch (graphics->graphics.flag_disposal_method) {
					// reserved previous frame pixels 
					case 0x01:
					{
						if (frame_count == 0) {
							if (graphics->graphics.flag_transparency_used == 1) {
								color_index_list[row * (*animation)->w + col] = graphics->graphics.transparent_color_index;
							}
							else if(gif->LogicalScreenDescriptor.flag_color_table == 1) {
								color_index_list[row * (*animation)->w + col] = gif->LogicalScreenDescriptor.bg_color_index;
							}
						}
					}
					break;

					// repainted out of image range
					case 0x02:
					{
						if (graphics->graphics.flag_transparency_used == 1) {
							color_index_list[row * (*animation)->w + col] = graphics->graphics.transparent_color_index;
						}
						else if(gif->LogicalScreenDescriptor.flag_color_table == 1) {
							color_index_list[row * (*animation)->w + col] = gif->LogicalScreenDescriptor.bg_color_index;
						}
					}
					break;

					// 3~7
					default:
						break;
					}
				}
				else if (frame_count != 0) {
					switch (graphics->graphics.flag_disposal_method) {
					// reserved transparent pixel as previous frame pixel 			
					case 0x01:
					{
						if ( graphics->graphics.flag_transparency_used == 1 && graphics->graphics.transparent_color_index == changed_color_index_list[global_part_index] )  {
							if (color_index_kind[row * (*animation)->w + col] == GLOBAL_COLOR) {
								frame[row * (*animation)->w + col].r = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].r;
								frame[row * (*animation)->w + col].g = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].g;
								frame[row * (*animation)->w + col].b = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].b;
							}
							else if (prev_image->image_descriptor.flag_color_table == 1) {
								frame[row * (*animation)->w + col].r = prev_image->local_color_table[color_index_list[row * (*animation)->w + col]].r;
								frame[row * (*animation)->w + col].g = prev_image->local_color_table[color_index_list[row * (*animation)->w + col]].g;
								frame[row * (*animation)->w + col].b = prev_image->local_color_table[color_index_list[row * (*animation)->w + col]].b;
							}
							else {
								frame[row * (*animation)->w + col].r = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].r;
								frame[row * (*animation)->w + col].g = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].g;
								frame[row * (*animation)->w + col].b = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].b;
							}
							
							++global_part_index;
							continue;
						}
						else {
							color_index_list[row * (*animation)->w + col] = changed_color_index_list[global_part_index];
							++global_part_index;
						}
					}
					break;

					// repainted every pixels in image range
					case 0x02:
					{
						color_index_list[row * (*animation)->w + col] = changed_color_index_list[global_part_index];
						++global_part_index;						
					}
					break;

					// 0 && 3~7
					default:
						break;
					}
				}
				else {
					color_index_list[row * (*animation)->w + col] = changed_color_index_list[global_part_index];
					++global_part_index;
				}

				if (image->image_descriptor.flag_color_table == 1) {
					frame[row * (*animation)->w + col].r = image->local_color_table[color_index_list[row * (*animation)->w + col]].r;
					frame[row * (*animation)->w + col].g = image->local_color_table[color_index_list[row * (*animation)->w + col]].g;
					frame[row * (*animation)->w + col].b = image->local_color_table[color_index_list[row * (*animation)->w + col]].b;
					color_index_kind[row * (*animation)->w + col] = LOCAL_COLOR;
				}
				else if (gif->LogicalScreenDescriptor.flag_color_table == 1) {
					frame[row * (*animation)->w + col].r = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].r;
					frame[row * (*animation)->w + col].g = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].g;
					frame[row * (*animation)->w + col].b = gif->GlobalColorTable[color_index_list[row * (*animation)->w + col]].b;
					color_index_kind[row * (*animation)->w + col] = GLOBAL_COLOR;
				}
			}
		}

		// interface (4 groups)
		// Group0: every 8 lines, start by line 0
		// Group1: every 8 lines, start by line 4
		// Group2: every 4 lines, start by line 2
		// Group3: every 2 lines, start by line 1
		if (image->image_descriptor.flag_interlace == 1) {
			IMG_FRAME *frame_raw = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * pixel_count);
			UINTN interlace_frame_line = 0;
			for (UINTN group = 0; group < 4; ++group) {
				UINTN current_line = 0;
				UINTN step_line = 0;
				switch (group) {
				case 0:
					current_line = 0;
					step_line = 8;
					break;
				case 1:
					current_line = 4;
					step_line = 8;
					break;
				case 2:
					current_line = 2;
					step_line = 4;
					break;
				case 3:
					current_line = 1;
					step_line = 2;
					break;
				default:
					printf("interface frame transform failed.\n");
					break;
				}

				// if you exchange `current_line` and `interlace_frame_line`, the image will be interlaced again.  
				while (current_line < (*animation)->h) {
					for (UINTN col = 0; col < (*animation)->w; ++col) {
						frame_raw[current_line * (*animation)->w + col].r = frame[interlace_frame_line * (*animation)->w + col].r;	
						frame_raw[current_line * (*animation)->w + col].g = frame[interlace_frame_line * (*animation)->w + col].g;	
						frame_raw[current_line * (*animation)->w + col].b = frame[interlace_frame_line * (*animation)->w + col].b;	
					}
					++interlace_frame_line;
					current_line += step_line;
				}
			}

			free(frame);
			frame = NULL;
			frame = frame_raw;
		}

		(*animation)->frames[frame_count] = frame;
		frame = NULL;
		++frame_count;
		prev_image = image;
		image = prev_image->next;
		graphics = graphics->next;

		free(changed_data);
		changed_data = NULL;
		free(changed_color_index_list);
		changed_color_index_list = NULL;
	}

	free(color_index_list);
	free(color_index_kind);
	return TRUE;
}

BOOL	GIFParserClearAnimation(IN IMG_ANIMATION *animation) {
	printf("Now function: GIFParserClearAnimation\n");
	if (animation) {
		for(UINTN i = 0; i < animation->count; ++i) {
			free(animation->frames[i]);
		}
		if (animation->frames) free(animation->frames);
		free(animation);
	}
	return TRUE;
}

BOOL  GIFParserGetGifDataFromFile(IN const CHAR *filename, OUT GIF **gif, OUT UINTN *buffer_size) {
	printf("Now Function: GIFParserGetGifDataFromFile\n");
	CHAR   *file_buffer = NULL;           // DO NOT FREE
	CHAR   *file_buffer_header = NULL;    // free this free file_buffer
	UINTN  file_size = 0;
	CHAR   c = 0;
	FILE *src = fopen(filename, "rb");
	
	file_size = _GetFileSizeByByte(src);
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

	(*gif)->FramesCount = 0;

	gAllocComponentCount = 1;
	(*gif)->ComponentOrder.component = (GIF_COMPONENT *)malloc(gAllocComponentCount*ALLOC_COMPONENT_SIZE);
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

UINT8 *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size) {
	printf("Now function: GIFParserGetDataBufferFromGif\n");
	UINT8 *buffer = (CHAR *)malloc(sizeof(UINT8) * buffer_size);
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
			}
			break;
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
			} 
			break;
			case kGraphicsExt:
			{
				if (g != NULL) {
					memcpy(buffer+index, &g->graphics.header.introducer, 8);
					index += 8;
					g = g->next;
				}
			}
			break;
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
			}
			break;
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
	if (gif->ImageDataHeader != NULL) {
		GIF_IMAGE_DATA *b = gif->ImageDataHeader;
		GIF_IMAGE_DATA *d = b->next;
		
		while (d != NULL) {
			GIF_DATA_SUB_BLOCK_NODE *p = d->one_frame_data.data_sub_block_buffer.header;
			GIF_DATA_SUB_BLOCK_NODE *q = p->next;

			while (q != NULL) {
				free(p);
				p = q;
				q = q->next;
			}
			free(p);

			if (b->local_color_table) {
				free(b->local_color_table);
			}

			free(b);
			b = d;
			d = d->next;
		}
		free(b);
	}

	if (gif->CommentExtHeader) {
		GIF_COMMENT_EXT_DATA *c = gif->CommentExtHeader;
		GIF_COMMENT_EXT_DATA *v = c->next;

		while (v != NULL) {
			GIF_DATA_SUB_BLOCK_NODE *p = v->comment.data_sub_block_buffer.header;
			GIF_DATA_SUB_BLOCK_NODE *q = p->next;

			while (q != NULL) {
				free(p);
				p = q;
				q = q->next;
			}
			free(p);

			free(c);
			c = v;
			v = v->next;
		} 
		free(c);
	}

	if (gif->AppExtHeader) {
		GIF_APP_EXT_DATA *a = gif->AppExtHeader;
		GIF_APP_EXT_DATA *s = a->next;

		while (s != NULL) {
			GIF_DATA_SUB_BLOCK_NODE *p = s->app.data_sub_block_buffer.header;
			GIF_DATA_SUB_BLOCK_NODE *q = p->next;

			while (q != NULL) {
				free(p);
				p = q;
				q = q->next;
			}
			free(p);

			free(a);
			a = s;
			s = s->next;
		} 
		free(a);
	}

	if (gif->GraphicsExtHeader) {
		GIF_GRAPHICS_EXT_DATA *g = gif->GraphicsExtHeader;
		GIF_GRAPHICS_EXT_DATA *h = g->next;
		while (h != NULL) {
			free(g);
			g = h;
			h = h->next;
		}
		free(g);
	}

	if (gif->ComponentOrder.component != NULL) {
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
		new_app_node->app.data_sub_block_buffer.total_data_size = 0;
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
			new_node->next = NULL;
			new_app_node->app.data_sub_block_buffer.total_data_size += size;
			_GIFParserMemRead(new_node->data, (VOID**)src, size);
			_GIFParserMemRead(&size, (VOID**)src, 1);
			p->next = new_node;
			p = p->next;
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
		new_com_node->comment.data_sub_block_buffer.total_data_size = 0;
		new_com_node->comment.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
		GIF_DATA_SUB_BLOCK_NODE *p = new_com_node->comment.data_sub_block_buffer.header;
		p->next = NULL;

		new_com_node->comment.header.introducer = 0x21;
		new_com_node->comment.header.label      = 0xFE;
		_GIFParserMemRead(&size, (VOID**)src, 1);
		while (size > 0) {
			GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
			new_node->data_size = size;
			new_node->next = NULL;
			new_com_node->comment.data_sub_block_buffer.total_data_size += size;
			_GIFParserMemRead(new_node->data, (VOID**)src, size);
			_GIFParserMemRead(&size, (VOID**)src, 1);
			p->next = new_node;
			p = p->next;
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
	new_image_node->one_frame_data.data_sub_block_buffer.total_data_size = 0;
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
		new_image_node->one_frame_data.data_sub_block_buffer.total_data_size += size;
		_GIFParserMemRead(new_node->data, (VOID**)src, size);
		p->next = new_node;
		p = p->next;
		_GIFParserMemRead(&size, (VOID**)src, 1);
	}

	if (size == 0) {  // one frame data: terminator, the last size is terminator = 0
		new_image_node->one_frame_data.terminator = 0;		
		gTailerPointer.image->next = new_image_node;
		gTailerPointer.image = new_image_node;
		++((*gif)->FramesCount);
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

UINTN _GetFileSizeByByte(IN FILE *fp) {
	UINTN file_size = 0;
	if ( fp == NULL ) {
		printf("GetFileSizeByByte: fp is NULL.\n");
	}

	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	rewind(fp);

	return file_size;
}

VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len) {
	FILE *fp = fopen("log", "ab");
	for (int i = 0; i < len; ++i) {
		fwrite(buffer+i, 1, 1, fp);
	}
	fclose(fp);
}

