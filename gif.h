#ifndef _GIF_
#define _GIF_

#include <stdint.h>

#pragma pack(1)

typedef uint8_t   CHAR;
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;

//extension block header
typedef struct GIF_EXTENSION_HEADER {
	CHAR    introducer;                                                               // 0x21  '!'
	CHAR    label;                                                                    // 0xFF 0xFE 0xF9 0x01     refer to readme.md GIF Essentials
} GIF_EXTENSION_HEADER;

// for comment extension and image data  sub data block
typedef struct GIF_DATA_SUB_BLOCK_NODE {
  UINT8                       data_size;                                            // 0x01~0xFF
  UINT8                       data[255];                                            // Max 0xFF
  struct GIF_DATA_SUB_BLOCK   *next;
} GIF_DATA_SUB_BLOCK_NODE;

typedef struct GIF_DATA_SUB_BLOCK {
  UINT32                    block_count;                                            // if block_count > 1 then there are block_count - 1 NODE.size = 255
  GIF_DATA_SUB_BLOCK_NODE   *header;
} GIF_DATA_SUB_BLOCK;

// ========================================== GIF FILE FORMAT ========================================================= //
// [6 Bytes] header
typedef struct GIF_HEADER {
	CHAR      gif_signature[3];                                                       // "GIF"
	CHAR      gif_version[3];                                                         // "87a" or "89a"
} GIF_HEADER;

// [7 Bytes] logical screen descriptor 
typedef struct GIF_LOGICAL_SCREEN_DESCRIPTOR {
	UINT16    canvas_width;																														// usually same as img width
  UINT16    canvas_height;																													// usually same as img height
	CHAR      flag_table_size : 3;                                                          // size of global table
	CHAR      flag_sort : 1;                                                          // sort flag 
	CHAR      flag_cr : 3;                                                            // color resolution 
	CHAR      flag_color_table : 1;                                                   // global color table flag 
	CHAR      bg_color_index;																													// usually 0
	CHAR      aspect_ratio;																														// usually 0
} GIF_LOGICAL_SCREEN_DESCRIPTOR;

// global/local color table, its amount depends on size of global/local table 2^(N+1) -> 0~255
// global color table
typedef struct GIF_COLOR_TABLE {
  UINT8 r;
  UINT8 g;
  UINT8 b;
} GIF_COLOR_TABLE;

// [19 Bytes] application extension                                         
typedef struct GIF_APP_EXTENSION {
	GIF_EXTENSION_HEADER    header;                                                   // 0xFF21
	UINT8                   size;                                                     // 11 (hex 0x0B) Length of Application Block
	CHAR                    identifier[8];                                            // "NETSCAPE"
	CHAR                    authentication_code[3];                                   // "2.0"
  UINT8                   sub_block_length;                                         // 3 (hex 0x03) Length of Data Sub-Block
	UINT8                   solid_value;                                              // 1
  UINT16                  loop_number;                                              // 0~65535   0 for forever loop
	CHAR terminator;                                                                  // 0x00
} GIF_APP_EXTENSION;

// comment extension                                             
typedef struct GIF_COMMENT_EXTENSION {
  GIF_EXTENSION_HEADER    header;                                                   // 0xFE21
  CHAR                    *data_sub_block_buffer;                                   // all NODE data in GIF_DATA_SUB_BLOCK
  CHAR                    terminator;                                               // 0x00
} GIF_COMMENT_EXTENSION;

// plain text extension - GIF89 (This feature never took off)    // 0x0121

// [8 Bytes] graphic control extension                                      
typedef struct GIF_GC_EXTENSION {
	GIF_EXTENSION_HEADER    header;                                                   // 0xF921
	CHAR                    size;                                                     // 0x04
	CHAR                    flag_transparency_used : 1;                                  // enable/disable transparency, if enable we need set a color key as transparency color
	CHAR                    flag_input : 1;                                           // user input
	CHAR                    flag_disposal_method : 3;                                 // 0~3 useful, 4~7 reserved for future 
	CHAR                    flag_reserved : 3;                                        // 000      
	UINT16                  delay_time;                                               // 1/100 s
	CHAR                    transparent_color_index;                                  // transparency color key
	CHAR                    terminator;                                               // 0x00
} GIF_GC_EXTENSION;

// [10 Bytes] image descriptor
typedef struct GIF_IMAGE_DESCRIPTOR {
	CHAR                    introducer;                                               // 0x2C  ','
	UINT16                  left;                                                     // image left
	UINT16                  top;                                                      // image top  
	UINT16                  width;                                                    // image width
	UINT16                  height;                                                   // image height
	CHAR                    flag_table_size : 3;                                            // size of local color table
	CHAR                    flag_reserved : 2;                                        // 00      
	CHAR                    flag_sort : 1;                                            // sort flag
	CHAR                    flag_interlace : 1;                                       // reduce annoying visual flicker. http://webtutor.tamu.edu/lesson6/interlace.html
	CHAR                    flag_color_table : 1;                                     // enable/disable local color table
} GIF_IMAGE_DESCRIPTOR;

/* local color table */

// image data
typedef struct GIF_ONE_FRAME_DATA {
  UINT8                   LZW_Minimum_Code;                                         // encode/decode need this
  GIF_DATA_SUB_BLOCK      data_sub_block_buffer;                                    // all NODE data GIF_DATA_SUB_BLOCK  
  UINT8                   terminator;                                               // 0x00
} GIF_ONE_FRAME_DATA;  

// [1 Bytes] trailer (just display struct, this value will be written in GIF structure directly)
typedef struct GIF_TRAILER {
	CHAR 										trailer;       																						// end of file 0x3B  ';'
} GIF_TRAILER;
// ============================================================ GIF FILE FORMAT =================================================================== //

// GIF IMG DATA NODE
typedef struct GIF_IMG_DATA_NODE {
	GIF_GC_EXTENSION              graphics_control_ext;
	GIF_IMAGE_DESCRIPTOR          image_descriptor;
	GIF_COLOR_TABLE               *local_color_table;
	GIF_ONE_FRAME_DATA            one_frame_data;
	struct GIF_ALL_IMG_DATA_NODE  *next;
} GIF_IMG_DATA_NODE;

// GIF IMG DATA
typedef struct GIF_IMG_DATA {
	UINT32                        pack_count;
	GIF_IMG_DATA_NODE         *header;
} GIF_IMG_DATA;

// GIF
typedef struct GIF {
	GIF_HEADER 													Header;                   // 6B
	GIF_LOGICAL_SCREEN_DESCRIPTOR 			LogicalScreenDescriptor;  // 7B
	GIF_COLOR_TABLE                     *GlobalColorTable;        // calculate size
	GIF_APP_EXTENSION                   *ApplicationExt;
	GIF_COMMENT_EXTENSION								*CommentExt;
	
	GIF_IMG_DATA                    		ImageData;
	CHAR                                trailer;  // 0x3B  ';'
} GIF;

#endif