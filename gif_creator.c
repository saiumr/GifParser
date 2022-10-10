/* This program creates a gif picture uses 7 colors */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <lzw/lzw.h>

typedef uint8_t byte;

int main(int argc, const char **argv) {
  FILE *fp = NULL;

  fp = fopen("test.gif", "wb");
  if (fp == NULL) {
    printf("fp is NULL.\n");
  }

  // GIF Header
  uint8_t gif_header[] = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61};
  fwrite(gif_header, 6, 1, fp);

  // Screen Logical Label
  uint16_t gif_width = 100;
  uint16_t gif_height = 100;
  // 0xF2 = 1   1 1 1   0   0 1 0
  uint8_t gif_logical_screen_pack_byte = 0xF2;  // low 3 bits is size of global table N. need 2^(N+1) colors.
  uint8_t gif_bg_color_index = 0;
  uint8_t gif_pixel_aspect = 0;
  fputc(gif_width, fp);
  fputc(gif_width >> 8, fp);
  fputc(gif_height, fp);
  fputc(gif_height >> 8, fp);
  fputc(gif_logical_screen_pack_byte, fp);
  fputc(gif_bg_color_index, fp);
  fputc(gif_pixel_aspect, fp);

  // Global Color Table
  uint32_t gif_global_color_table[] = {
    0xFF0000,
    0xFFA500,
    0xFFFF00,
    0x00FF00,
    0x007FFF,
    0x0000FF,
    0x8B00FF,
    0x000000     // occupy space color, not use
  };
  for (int i = 0; i < 8; ++i) {
    uint8_t r = (gif_global_color_table[i] & 0xFF0000) >> 16;
    uint8_t g = (gif_global_color_table[i] & 0x00FF00) >> 8;
    uint8_t b = (gif_global_color_table[i] & 0x0000FF);
    fputc(r, fp);
    fputc(g, fp);
    fputc(b, fp);
  }

  // Application Extension
  uint8_t gif_application_extension[] = {0x21, 0xFF, 0x0B, 0x4E, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00};
  fwrite(gif_application_extension, 19, 1, fp);

  // Comment Extension
  // Created with ezgif.com GIF maker
  uint8_t gif_comment_extension[] = {0x21, 0xFE, 0x20, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x64, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x65, 0x7A, 0x67, 0x69, 0x66, 0x2E, 0x63, 0x6F, 0x6D, 0x20, 0x47, 0x49, 0x46, 0x20, 0x6D, 0x61, 0x6B, 0x65, 0x72, 0x00};
  fwrite(gif_comment_extension, 36, 1, fp);

  // Find out GCE(Graphic Control Extension) amount find out how many frames there is. GCE start with "0xF921".
  for (int i = 0; i < 8; ++i) {
    // Graphic Control Extension  (0x21, 0xF9, 0x04)  
    uint8_t gif_graphic_control_extension[] = {0x21, 0xF9, 0x04, 0x00, 0x32, 0x00, 0xFF, 0x00};
    fwrite(gif_graphic_control_extension, 8, 1, fp);

    // Image Descriptor
    uint8_t gif_image_descriptor[] = {0x2C, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x64, 0x00, 0x00};
    fwrite(gif_image_descriptor, 10, 1, fp);

    // one frame data color by index
    uint8_t *gif_one_frame_raw = malloc(700 * 700);
    memset(gif_one_frame_raw, i, 700*700);              // set 0 for 700*700 data 
    printf("current frame color index: %d\n", gif_one_frame_raw[0]);
    unsigned long compressed_size = 0;
    byte *img = NULL;

    // ======== storage raw_before_compress for comparing =============== //
    FILE *raw = fopen("raw_before_compress.raw", "ab");
    fwrite(gif_one_frame_raw, 700*700, 1, raw);
    fflush(raw);
    fclose(raw);

    lzw_compress_gif(3, 700*700, gif_one_frame_raw, &compressed_size, &img);  // 3 is LZW Minimum Code Size (see Docs "What is GIF")
    printf("compress_len = %u\n", compressed_size);
    
    uint8_t *gif_raw = malloc(700*700);
    unsigned long raw_len = 0;
    lzw_decompress(3, 700*700, img, &raw_len, &gif_raw);
    printf("raw_len = %d\n", raw_len);
    FILE *gif_res = fopen("gif_raw_res.raw", "ab");
    fwrite(gif_raw, 700*700, 1, gif_res);
    fflush(gif_res);
    fclose(gif_res);

    printf("size img = %u, compress_size = %u", sizeof(img), compressed_size);
    
    printf("current frame compression size: %ld\n", compressed_size);
    fputc(0x03, fp);  // 3 is LZW Minimum Code Size (see Docs "What is GIF")
    unsigned long current_index = 0;
    int count = 0;
    while (current_index < compressed_size) {
        printf("repeat?: %d\n", count);
        ++count;
        // In GIF, data sub-block (block followed LZW minimum code size) could repeat many time, and first byte is size of sub-block, it should be 0~0xFF   
        if((current_index + 0xFF) >= compressed_size) {             // compress_size < 255, directly writing in file else press 0xFF bytes in file looped.
            unsigned long diff = compressed_size - current_index;  
            fputc(diff, fp);                          // data size
            fwrite(img+current_index, diff, 1, fp);   // data
            fputc(0x00, fp);
            current_index += diff;
        } else {
            fputc(0xFF, fp);                          // data size
            fwrite(img+current_index, 0xFF, 1, fp);   // data
            current_index += 0xFF;
        }
    }
    free(gif_one_frame_raw);
    free(img);
  }

  fputc(0x3B, fp);
  fflush(fp);
  fclose(fp);

  return 0;
}