/* This pragma will parse (*gif) picture created by gif_creator */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <lzw/lzw.h>
#include "gif_parser.h"

// the component order list grows in blocks of this many entries; element size is
// applied at the allocation site (the old macro already multiplied by the element
// size and was then multiplied again, wasting ~1MB per parse)
#define ALLOC_COMPONENT_AMOUNT 512
UINT16 gAllocComponentCount = 1;

UINTN _GetFileSizeByByte(IN FILE *fp);

/* ---------------------------------------------------------------------------
 * Bounds-checked read cursor.
 *
 * The parser used to walk a bare `CHAR *` through the file buffer with no idea
 * how many bytes were left, so a truncated or hostile file simply read past the
 * allocation (a 6-byte file was enough to trip the stack protector). Every read
 * now goes through this cursor, which records a hard error instead of running
 * off the end.
 *
 * `pos` deliberately has no "valid" sentinel: `bad` carries that, so that a
 * cursor sitting exactly at end-of-buffer is still representable.
 * ------------------------------------------------------------------------- */
typedef struct
{
    const UINT8 *data;
    UINTN size;
    UINTN pos;
    BOOL bad; // set when a read went past the end
} _GIF_READER;

static void _GIFReaderInit(IN OUT _GIF_READER *r, IN const void *data, IN UINTN size)
{
    r->data = (const UINT8 *)data;
    r->size = size;
    r->pos = 0;
    r->bad = FALSE;
}

static BOOL _GIFReaderHas(IN const _GIF_READER *r, IN UINTN n)
{
    return (!r->bad) && (r->size - r->pos >= n);
}

// read one byte, or -1 if the file ended
static int _GIFReaderByte(IN OUT _GIF_READER *r)
{
    if (!_GIFReaderHas(r, 1))
    {
        r->bad = TRUE;
        return -1;
    }
    return r->data[r->pos++];
}

// copy n bytes out of the cursor
static BOOL _GIFReaderCopy(IN OUT _GIF_READER *r, OUT void *dst, IN UINTN n)
{
    if (!_GIFReaderHas(r, n))
    {
        r->bad = TRUE;
        return FALSE;
    }
    memcpy(dst, r->data + r->pos, n);
    r->pos += n;
    return TRUE;
}

// advance without copying
static BOOL _GIFReaderSkip(IN OUT _GIF_READER *r, IN UINTN n)
{
    if (!_GIFReaderHas(r, n))
    {
        r->bad = TRUE;
        return FALSE;
    }
    r->pos += n;
    return TRUE;
}

BOOL _HandleExtension(IN OUT _GIF_READER *r, IN CHAR label, OUT GIF **gif);
BOOL _HandleImageData(IN OUT _GIF_READER *r, OUT GIF **gif);

// consume a data sub-block chain: [size][data...]...[0x00]
BOOL _SkipDataSubBlocks(IN OUT _GIF_READER *r);

VOID _RecordComponentOrder(IN GIF_COMPONENT key, OUT GIF **gif);

VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len);

typedef struct
{
    GIF_APP_EXT_DATA *app;
    GIF_COMMENT_EXT_DATA *comment;
    GIF_GRAPHICS_EXT_DATA *graphics;
    GIF_IMAGE_DATA *image;
} _GIF_TAILER_POINTER;

_GIF_TAILER_POINTER gTailerPointer;


BOOL GIFParserGetAnimationFromFile(IN const CHAR *filename, OUT IMG_ANIMATION **animation)
{
    printf("Now function: GIFParserGetAnimationFromFile\n");
    GIF *gif = NULL;
    UINTN buffer_size = 0;
    if (!GIFParserGetGifDataFromFile(filename, &gif, &buffer_size))
    {
        return FALSE;
    }

    if (!GIFParserGetAnimationFromGif(gif, animation))
    {
        // the parsed GIF structure is fully owned here: without this the whole
        // file structure (color tables, sub-blocks, frame list) leaked whenever
        // the file decoded but could not be turned into an animation
        GIFParserClear(gif);
        return FALSE;
    }

    GIFParserClear(gif);

    return TRUE;
}

// index the canvas starts from and that a short frame is padded with: the
// Logical Screen Descriptor background index when a global table exists
static UINT8 _AnimationBackgroundIndex(IN const IMG_ANIMATION *animation)
{
    return (animation->global_palette != NULL) ? animation->background_index : 0;
}

BOOL GIFParserGetAnimationFromGif(IN GIF *gif, OUT IMG_ANIMATION **animation)
{
    printf("Now function: GIFParserGetAnimationFromGif\n");
    if (gif == NULL || animation == NULL)
    {
        return FALSE;
    }
    *animation = NULL;

    if (gif->FramesCount == 0)
    {
        printf("GIFParserGetAnimationFromGif: no frames in file.\n");
        return FALSE;
    }

    IMG_ANIMATION *result = (IMG_ANIMATION *)calloc(1, sizeof(IMG_ANIMATION));
    if (result == NULL)
    {
        return FALSE;
    }

    result->width = gif->LogicalScreenDescriptor.canvas_width;
    result->height = gif->LogicalScreenDescriptor.canvas_height;
    result->count = gif->FramesCount;
    result->background_index = gif->LogicalScreenDescriptor.bg_color_index;

    if (gif->LogicalScreenDescriptor.flag_color_table == 1 && gif->GlobalColorTable != NULL)
    {
        /* deep-copy the global table: the caller frees the parsed GIF before
           using the animation, and frames render through this palette */
        UINTN entries = (UINTN)1 << (gif->LogicalScreenDescriptor.flag_table_size + 1);
        result->global_palette = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * entries);
        if (result->global_palette == NULL)
        {
            free(result);
            return FALSE;
        }
        memcpy(result->global_palette, gif->GlobalColorTable, sizeof(GIF_COLOR_TABLE) * entries);
        result->global_palette_entries = entries;
    }

    if (result->width == 0 || result->height == 0)
    {
        printf("GIFParserGetAnimationFromGif: degenerate canvas %lux%lu.\n",
               result->width, result->height);
        free(result);
        return FALSE;
    }

    result->frames = (GIF_FRAME_INFO *)calloc(result->count, sizeof(GIF_FRAME_INFO));
    if (result->frames == NULL)
    {
        free(result);
        return FALSE;
    }

    printf("true width/height: %lu, %lu\n", result->width, result->height);

    /* A Graphic Control Extension is optional: an image block with no preceding
       GCE implies disposal 0, no transparency and no delay. Keep a zeroed default
       so the fields can be read unconditionally. */
    GIF_GRAPHICS_EXT_DATA default_graphics;
    memset(&default_graphics, 0, sizeof(default_graphics));

    GIF_IMAGE_DATA *image = gif->ImageDataHeader->next;
    GIF_GRAPHICS_EXT_DATA *graphics = gif->GraphicsExtHeader->next;

    for (UINTN frame_count = 0; frame_count < result->count; ++frame_count)
    {
        if (image == NULL)
        {
            printf("GIFParserGetAnimationFromGif: image list ended early at frame %lu.\n", frame_count);
            GIFParserClearAnimation(result);
            return FALSE;
        }

        GIF_GRAPHICS_CONTROL_EXTENSION *gce = (graphics != NULL) ? &graphics->graphics
                                                                : &default_graphics.graphics;
        GIF_FRAME_INFO *out = &result->frames[frame_count];

        out->left = image->image_descriptor.left;
        out->top = image->image_descriptor.top;
        out->width = image->image_descriptor.width;
        out->height = image->image_descriptor.height;
        out->interlaced = (image->image_descriptor.flag_interlace == 1);
        out->has_transparency = (gce->flag_transparency_used == 1);
        out->transparent_index = gce->transparent_color_index;
        out->disposal_method = gce->flag_disposal_method;
        out->delay_ms = (UINT32)gce->delay_time * 10; /* GIF delays are 1/100 s */
        if (image->image_descriptor.flag_color_table == 1 && image->local_color_table != NULL)
        {
            /* The frame gets its own copy: the parsed GIF is freed before the
               animation is handed back, so a borrowed pointer would dangle. */
            UINTN entries = (UINTN)1 << (image->image_descriptor.flag_table_size + 1);
            out->palette = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * entries);
            if (out->palette == NULL)
            {
                GIFParserClearAnimation(result);
                return FALSE;
            }
            memcpy(out->palette, image->local_color_table, sizeof(GIF_COLOR_TABLE) * entries);
            out->palette_entries = entries;
        }
        else
        {
            out->palette = result->global_palette;
            out->palette_entries = result->global_palette_entries;
        }

        UINTN rect_pixels = (UINTN)out->width * (UINTN)out->height;
        printf("frame count: [%lu]\n", frame_count);
        printf("w = %u, h = %u, left = %u, top = %u\n", image->image_descriptor.width,
               image->image_descriptor.height, image->image_descriptor.left, image->image_descriptor.top);

        if (rect_pixels == 0)
        {
            out->pixels = NULL; /* an empty rectangle draws nothing */
            goto next_frame;
        }

        {
            /* Concatenate the data sub-block chain into one contiguous payload */
            UINTN payload_size = image->one_frame_data.data_sub_block_buffer.total_data_size;
            UINT8 *changed_data = (UINT8 *)malloc(payload_size ? payload_size : 1);
            if (changed_data == NULL)
            {
                GIFParserClearAnimation(result);
                return FALSE;
            }
            UINTN payload_used = 0;
            for (GIF_DATA_SUB_BLOCK_NODE *p = image->one_frame_data.data_sub_block_buffer.header->next;
                 p != NULL; p = p->next)
            {
                memcpy(changed_data + payload_used, p->data, p->data_size);
                payload_used += p->data_size;
            }

            /* lzw_decompress allocates the output buffer itself and hands it back
               through the pointer argument, so this must NOT be pre-allocated:
               passing a freshly malloc'd buffer here would leak it on every frame
               (the callee overwrites the pointer without freeing it). */
            UINT8 *index_buffer = NULL;
            UINTN decoded = 0;
            lzw_decompress(image->one_frame_data.LZW_Minimum_Code, payload_used, changed_data,
                           rect_pixels, &decoded, &index_buffer);
            printf("changed_data_index: %lu, changed_data_size: %lu, out_changed_data_size: %lu\n",
                   payload_used, rect_pixels, decoded);
            if (index_buffer == NULL)
            {
                free(changed_data);
                GIFParserClearAnimation(result);
                return FALSE;
            }

            /* A malformed or truncated frame may decode to fewer pixels than its
               rectangle declares; the missing tail must not be undefined. */
            if (decoded < rect_pixels)
            {
                printf("GIFParserGetAnimationFromGif: frame %lu decoded %lu of %lu pixels, padding with background.\n",
                       frame_count, decoded, rect_pixels);
                memset(index_buffer + decoded, _AnimationBackgroundIndex(result), rect_pixels - decoded);
            }

            out->pixels = index_buffer;
            free(changed_data);
        }

    next_frame:
        image = image->next;
        if (graphics != NULL)
        {
            graphics = graphics->next;
        }
    }

    *animation = result;
    return TRUE;
}


BOOL GIFParserClearAnimation(IN IMG_ANIMATION *animation)
{
    printf("Now function: GIFParserClearAnimation\n");
    if (animation == NULL)
    {
        return FALSE;
    }
    if (animation->frames != NULL)
    {
        for (UINTN i = 0; i < animation->count; ++i)
        {
            free(animation->frames[i].pixels);
            animation->frames[i].pixels = NULL;
            /* only the frames that own a private table free one: frames without
               a table point at global_palette, which is released below */
            if (animation->frames[i].palette != NULL &&
                animation->frames[i].palette != animation->global_palette)
            {
                free(animation->frames[i].palette);
                animation->frames[i].palette = NULL;
            }
        }
        free(animation->frames);
    }
    free(animation->global_palette);
    free(animation);
    return TRUE;
}

BOOL GIFParserGetGifDataFromFile(IN const CHAR *filename, OUT GIF **gif, OUT UINTN *buffer_size)
{
    printf("Now Function: GIFParserGetGifDataFromFile\n");
    UINT8 *file_buffer = NULL; // owns the whole file
    UINTN file_size = 0;
    FILE *src = NULL;

    if (gif == NULL || buffer_size == NULL || filename == NULL)
    {
        printf("GIFParserGetGifDataFromFile: invalid argument.\n");
        return FALSE;
    }
    *gif = NULL;
    *buffer_size = 0;

    // open and size first: the previous version called _GetFileSizeByByte(src)
    // and fread() before checking whether fopen had even succeeded
    src = fopen((const char *)filename, "rb");
    if (src == NULL)
    {
        printf("GIFParserGetGifDataFromFile: cannot open \"%s\".\n", (const char *)filename);
        return FALSE;
    }

    file_size = _GetFileSizeByByte(src);
    *buffer_size = file_size;

    // 6 byte header + 7 byte logical screen descriptor is the smallest thing that
    // can even be called a GIF
    if (file_size < 13)
    {
        printf("GIFParserGetGifDataFromFile: file too small (%lu bytes), not a GIF.\n", file_size);
        fclose(src);
        return FALSE;
    }

    file_buffer = (UINT8 *)malloc(file_size);
    if (file_buffer == NULL)
    {
        printf("GIFParserGetGifDataFromFile: cannot allocate %lu byte buffer.\n", file_size);
        fclose(src);
        return FALSE;
    }
    if (fread(file_buffer, file_size, 1, src) != 1)
    {
        printf("GIFParserGetGifDataFromFile: short read.\n");
        free(file_buffer);
        fclose(src);
        return FALSE;
    }
    fclose(src);

    (*gif) = (GIF *)malloc(sizeof(GIF));
    if ((*gif) == NULL)
    {
        printf("GIFParserGetGifDataFromFile: cannot allocate GIF structure.\n");
        free(file_buffer);
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

    (*gif)->GraphicsExtHeader = (GIF_GRAPHICS_EXT_DATA *)malloc(sizeof(GIF_GRAPHICS_EXT_DATA));
    (*gif)->GraphicsExtHeader->next = NULL;
    gTailerPointer.graphics = (*gif)->GraphicsExtHeader;

    (*gif)->ImageDataHeader = (GIF_IMAGE_DATA *)malloc(sizeof(GIF_IMAGE_DATA));
    (*gif)->ImageDataHeader->next = NULL;
    gTailerPointer.image = (*gif)->ImageDataHeader;
    (*gif)->ImageDataHeader->local_color_table = NULL;

    (*gif)->FramesCount = 0;
    (*gif)->trailer_tail = NULL;
    (*gif)->trailer_tail_size = 0;

    gAllocComponentCount = 1;
    (*gif)->ComponentOrder.component = (GIF_COMPONENT *)malloc(gAllocComponentCount * ALLOC_COMPONENT_AMOUNT * sizeof(GIF_COMPONENT));
    (*gif)->ComponentOrder.size = 0;

    // Header Block 6 bytes
    _GIF_READER reader;
    _GIFReaderInit(&reader, file_buffer, file_size);
    _GIFReaderCopy(&reader, &(*gif)->Header, 6);

    if (strncmp((const char *)(*gif)->Header.gif_signature, "GIF", 3) != 0)
    {
        printf("GIFParserGetGifDataFromFile: this is not GIF file.\n");
        free(file_buffer);
        GIFParserClear(*gif);
        *gif = NULL;
        return FALSE;
    }

    // 87a files have no extensions but the block grammar is identical, so accept
    // both revisions instead of rejecting perfectly readable files
    if (strncmp((const char *)(*gif)->Header.gif_version, "87a", 3) != 0 &&
        strncmp((const char *)(*gif)->Header.gif_version, "89a", 3) != 0)
    {
        printf("GIFParserGetGifDataFromFile: unsupported GIF revision \"%.3s\".\n",
               (const char *)(*gif)->Header.gif_version);
        free(file_buffer);
        GIFParserClear(*gif);
        *gif = NULL;
        return FALSE;
    }

    // Logical Screen Descriptor 7 Bytes
    _GIFReaderCopy(&reader, &(*gif)->LogicalScreenDescriptor, 7);

    // Global color table
    (*gif)->GlobalColorTable = NULL;
    if ((*gif)->LogicalScreenDescriptor.flag_color_table == 1)
    {
        UINT16 global_color_table_amount = 1 << ((*gif)->LogicalScreenDescriptor.flag_table_size + 1);
        (*gif)->GlobalColorTable = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * global_color_table_amount);
        if ((*gif)->GlobalColorTable == NULL ||
            !_GIFReaderCopy(&reader, (*gif)->GlobalColorTable, sizeof(GIF_COLOR_TABLE) * global_color_table_amount))
        {
            printf("GIFParserGetGifDataFromFile: truncated global color table.\n");
            free(file_buffer);
            GIFParserClear(*gif);
            *gif = NULL;
            return FALSE;
        }
    }

    // Extensions and Image Data
    for (;;)
    {
        int c = _GIFReaderByte(&reader);

        if (c < 0)
        {
            printf("GIFParserGetGifDataFromFile: unexpected end of file (no trailer).\n");
            break;
        }

        // 0x3B
        if (c == ';')
        {
            (*gif)->trailer = 0x3B;
            printf("GIFParserGetGifDataFromFile: trailer at offset %lu of %lu.\n",
                   (unsigned long)(reader.pos - 1), (unsigned long)file_size);
            /* Anything left after the trailer is recorded at the end of the parse
               (see below), not here: a 0x3B can also appear inside block data, so
               the byte that ends the document is the one the parse actually
               stopped at, not necessarily the first 0x3B seen. */
            break;
        }

        // 0x21
        if (c == '!')
        {
            int label = _GIFReaderByte(&reader);
            if (label < 0)
            {
                printf("GIFParserGetGifDataFromFile: truncated extension header.\n");
                break;
            }
            if (!_HandleExtension(&reader, (CHAR)label, gif))
            {
                printf("GIFParserGetGifDataFromFile: extension %02X failed, stopping.\n", (unsigned)(UINT8)label);
                break;
            }
            if (reader.bad)
            {
                printf("GIFParserGetGifDataFromFile: truncated extension data, stopping.\n");
                break;
            }
            continue;
        }

        // 0x2C
        if (c != ',')
        {
            continue;
        }

        // Image Descriptor
        if (!_HandleImageData(&reader, gif) || reader.bad)
        {
            printf("GIFParserGetGifDataFromFile: image block failed at offset %lu of %lu, stopping.\n",
                   (unsigned long)reader.pos, (unsigned long)reader.size);
            break;
        }
    }

    if (reader.bad)
    {
        printf("GIFParserGetGifDataFromFile: file ended early, %lu frames recovered.\n",
               (unsigned long)(*gif)->FramesCount);
    }

    /* Bytes the document did not consume (some encoders append data after the
       trailer - 16dapipi.gif carries 16 of them) are kept so that a parse ->
       rebuild round trip stays byte-exact. Recording this once, after the loop,
       uses the parse's real end rather than the first 0x3B encountered. */
    if (reader.pos < file_size)
    {
        UINTN tail = file_size - reader.pos;
        (*gif)->trailer_tail = (CHAR *)malloc(tail);
        if ((*gif)->trailer_tail != NULL)
        {
            memcpy((*gif)->trailer_tail, file_buffer + reader.pos, tail);
            (*gif)->trailer_tail_size = tail;
            printf("GIFParserGetGifDataFromFile: %lu byte(s) after the trailer preserved.\n",
                   (unsigned long)tail);
        }
    }

    free(file_buffer);
    return TRUE;
}

UINT8 *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size)
{
    printf("Now function: GIFParserGetDataBufferFromGif\n");
    UINT8 *buffer = (CHAR *)malloc(sizeof(UINT8) * buffer_size);
    UINTN index = 0;
    UINTN size = 0;

    // header && logical screen descriptor
    size = 13;
    memcpy(buffer + index, gif, size);
    index += size;

    // Global color table
    if (gif->GlobalColorTable)
    {
        size = 1 << (gif->LogicalScreenDescriptor.flag_table_size + 1);
        memcpy(buffer + index, gif->GlobalColorTable, size * 3);
        index += size * 3;
    }

    // extension and image data
    GIF_APP_EXT_DATA *a = gif->AppExtHeader->next;
    GIF_COMMENT_EXT_DATA *c = gif->CommentExtHeader->next;
    GIF_GRAPHICS_EXT_DATA *g = gif->GraphicsExtHeader->next;
    GIF_IMAGE_DATA *i = gif->ImageDataHeader->next;
    for (UINTN component_index = 0; component_index < gif->ComponentOrder.size; ++component_index)
    {
        switch (gif->ComponentOrder.component[component_index])
        {
        case kAppExt:
        {
            if (a != NULL)
            {
                memcpy(buffer + index, &a->app.header.introducer, 14);
                index += 14;

                GIF_DATA_SUB_BLOCK_NODE *p = a->app.data_sub_block_buffer.header->next;
                while (p != NULL)
                {
                    memcpy(buffer + index, &p->data_size, 1);
                    index += 1;

                    size = p->data_size;
                    memcpy(buffer + index, p->data, size);
                    index += size;

                    p = p->next;
                }

                memcpy(buffer + index, &a->app.terminator, 1);
                index += 1;
                a = a->next;
            }
        }
        break;
        case kCommentExt:
        {
            if (c != NULL)
            {
                memcpy(buffer + index, &c->comment.header.introducer, 2);
                index += 2;

                GIF_DATA_SUB_BLOCK_NODE *p = c->comment.data_sub_block_buffer.header->next;
                while (p != NULL)
                {
                    memcpy(buffer + index, &p->data_size, 1);
                    index += 1;

                    size = p->data_size;
                    memcpy(buffer + index, p->data, size);
                    index += size;

                    p = p->next;
                }

                memcpy(buffer + index, &c->comment.terminator, 1);
                index += 1;
                c = c->next;
            }
        }
        break;
        case kGraphicsExt:
        {
            if (g != NULL)
            {
                memcpy(buffer + index, &g->graphics.header.introducer, 8);
                index += 8;                g = g->next;
            }
        }
        break;
        case kImageData:
        {
            if (i != NULL)
            {
                memcpy(buffer + index, &i->image_descriptor, 10);
                index += 10;

                if (i->local_color_table)
                {
                    size = 1 << (i->image_descriptor.flag_table_size + 1);
                    memcpy(buffer + index, i->local_color_table, size * 3);
                    index += size * 3;
                }

                memcpy(buffer + index, &i->one_frame_data.LZW_Minimum_Code, 1);
                index += 1;

                GIF_DATA_SUB_BLOCK_NODE *p = i->one_frame_data.data_sub_block_buffer.header->next;
                while (p != NULL)
                {
                    memcpy(buffer + index, &p->data_size, 1);
                    index += 1;

                    size = p->data_size;
                    memcpy(buffer + index, p->data, size);
                    index += size;
                    p = p->next;
                }
                memcpy(buffer + index, &i->one_frame_data.terminator, 1);
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
    memcpy(buffer + index, &gif->trailer, 1);
    index += 1;

    // re-emit any bytes the file carried after the trailer
    if (gif->trailer_tail != NULL && gif->trailer_tail_size > 0)
    {
        memcpy(buffer + index, gif->trailer_tail, gif->trailer_tail_size);
        index += gif->trailer_tail_size;
    }

    printf("GIFParserGetDataBufferFromGif: index = %lu\n", index);
    return buffer;
}

// free every real node of a list that starts with a dummy sentinel head.
// `free_node` may release node-owned resources (the image nodes own a local
// color table) and runs *before* the node itself is freed.
static void _GIFListDestroy(IN void *head, IN UINTN next_offset, IN void (*free_node)(void *))
{
    UINT8 *node = (head != NULL) ? *(UINT8 **)((UINT8 *)head + next_offset) : NULL;
    while (node != NULL)
    {
        UINT8 *following = *(UINT8 **)(node + next_offset);
        if (free_node != NULL)
        {
            free_node(node);
        }
        free(node);
        node = following;
    }
    free(head); // the sentinel itself
}

static void _GIFSubBlockChainFree(IN void *head);

// image nodes own their local color table *and* their compressed data sub-block
// chain; the old code freed the previous node's table while stepping, which
// leaked the table of every frame but the last (16dapipi.gif leaks 108 per parse)
static void _GIFImageNodeFree(IN void *node)
{
    GIF_IMAGE_DATA *image = (GIF_IMAGE_DATA *)node;
    free(image->local_color_table);
    image->local_color_table = NULL;
    _GIFSubBlockChainFree(image->one_frame_data.data_sub_block_buffer.header);
    image->one_frame_data.data_sub_block_buffer.header = NULL;
}

static void _GIFSubBlockChainFree(IN void *head)
{
    GIF_DATA_SUB_BLOCK_NODE *p = (GIF_DATA_SUB_BLOCK_NODE *)head;
    while (p != NULL)
    {
        GIF_DATA_SUB_BLOCK_NODE *q = p->next;
        free(p);
        p = q;
    }
}

static void _GIFCommentNodeFree(IN void *node)
{
    _GIFSubBlockChainFree(((GIF_COMMENT_EXT_DATA *)node)->comment.data_sub_block_buffer.header);
}

static void _GIFAppNodeFree(IN void *node)
{
    _GIFSubBlockChainFree(((GIF_APP_EXT_DATA *)node)->app.data_sub_block_buffer.header);
}

static void _GIFGraphicsNodeFree(IN void *node)
{
    (void)node;
}

BOOL GIFParserClear(IN GIF *gif)
{
    if (gif == NULL)
    {
        return FALSE;
    }
    printf("Now function: GIFParserClear\n");

    _GIFListDestroy(gif->ImageDataHeader, offsetof(GIF_IMAGE_DATA, next), _GIFImageNodeFree);
    _GIFListDestroy(gif->CommentExtHeader, offsetof(GIF_COMMENT_EXT_DATA, next), _GIFCommentNodeFree);
    _GIFListDestroy(gif->AppExtHeader, offsetof(GIF_APP_EXT_DATA, next), _GIFAppNodeFree);
    _GIFListDestroy(gif->GraphicsExtHeader, offsetof(GIF_GRAPHICS_EXT_DATA, next), _GIFGraphicsNodeFree);

    free(gif->ComponentOrder.component);
    free(gif->GlobalColorTable);
    free(gif->trailer_tail);
    free(gif);
    return TRUE;
}

// scan a data sub-block chain: [size][data...]...[0x00]
// returns the number of payload bytes skipped, or (UINTN)-1 if the chain never
// terminated (the file is truncated; the caller must treat it as an error)
BOOL _SkipDataSubBlocks(IN OUT _GIF_READER *r)
{
    UINTN skipped = 0;

    for (;;)
    {
        int size = _GIFReaderByte(r);
        if (size < 0)
        {
            return FALSE; // ran out of file before the terminator
        }
        if (size == 0)
        {
            return TRUE; // terminator
        }
        if (!_GIFReaderSkip(r, (UINTN)size))
        {
            return FALSE;
        }
        skipped += (UINTN)size;
    }
}

BOOL _HandleExtension(IN OUT _GIF_READER *r, IN CHAR label, OUT GIF **gif)
{
    int byte = 0;

    switch (label)
    {
    case 0x01: // Plain Text Extension - GIF89a, "this feature never took off"
    {
        // fixed header: [size=12][left,top,width,height][cell w,h][fg,bg color]
        // then the text itself follows as a data sub-block chain.
        // We do not render text, but the bytes must still be consumed, otherwise
        // the main loop would mistake them for the next block introducer.
        byte = _GIFReaderByte(r);
        if (byte < 0 || !_GIFReaderSkip(r, (UINTN)byte) || !_SkipDataSubBlocks(r))
        {
            printf("_HandleExtension[0x01]: truncated Plain Text Extension.\n");
            return FALSE;
        }
        return TRUE;
    }

    case 0xFF: // Application Extension  19bytes
    {
        if (gTailerPointer.app == NULL || gTailerPointer.app->next != NULL)
        {
            printf("gTailerPointer.app error\n");
            return FALSE;
        }

        // [size=11][identifier 8][auth 3] then the sub-block chain
        byte = _GIFReaderByte(r);
        if (byte < 0)
        {
            printf("_HandleExtension[0xFF]: truncated application header.\n");
            return FALSE;
        }

        GIF_APP_EXT_DATA *new_app_node = (GIF_APP_EXT_DATA *)malloc(sizeof(GIF_APP_EXT_DATA));
        if (new_app_node == NULL)
        {
            return FALSE;
        }
        memset(new_app_node, 0, sizeof(GIF_APP_EXT_DATA));
        new_app_node->app.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
        if (new_app_node->app.data_sub_block_buffer.header == NULL)
        {
            free(new_app_node);
            return FALSE;
        }
        GIF_DATA_SUB_BLOCK_NODE *p = new_app_node->app.data_sub_block_buffer.header;
        p->next = NULL;

        new_app_node->app.header.introducer = 0x21;
        new_app_node->app.header.label = 0xFF;
        new_app_node->app.size = (UINT8)byte;
        if (!_GIFReaderCopy(r, &new_app_node->app.identifier, sizeof(new_app_node->app.identifier) +
                                                                  sizeof(new_app_node->app.authentication_code)))
        {
            printf("_HandleExtension[0xFF]: truncated application identifier.\n");
            free(p);
            free(new_app_node);
            return FALSE;
        }

        for (;;)
        {
            int size = _GIFReaderByte(r);
            if (size < 0)
            {
                printf("_HandleExtension[0xFF]: truncated data sub-block.\n");
                free(p);
                free(new_app_node);
                return FALSE;
            }
            if (size == 0)
            {
                break;
            }
            GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
            if (new_node == NULL)
            {
                free(p);
                free(new_app_node);
                return FALSE;
            }
            new_node->data_size = (UINT8)size;
            new_node->next = NULL;
            if (!_GIFReaderCopy(r, new_node->data, (UINTN)size))
            {
                printf("_HandleExtension[0xFF]: truncated data sub-block payload.\n");
                free(new_node);
                free(p);
                free(new_app_node);
                return FALSE;
            }
            new_app_node->app.data_sub_block_buffer.total_data_size += (UINTN)size;
            p->next = new_node;
            p = new_node;
        }

        new_app_node->app.terminator = 0;
        gTailerPointer.app->next = new_app_node;
        gTailerPointer.app = new_app_node;
        _RecordComponentOrder(kAppExt, gif);
        return TRUE;
    }

    case 0xFE: // Comment Extension
    {
        if (gTailerPointer.comment == NULL || gTailerPointer.comment->next != NULL)
        {
            printf("gTailerPointer.comment error\n");
            return FALSE;
        }
        GIF_COMMENT_EXT_DATA *new_com_node = (GIF_COMMENT_EXT_DATA *)malloc(sizeof(GIF_COMMENT_EXT_DATA));
        if (new_com_node == NULL)
        {
            return FALSE;
        }
        memset(new_com_node, 0, sizeof(GIF_COMMENT_EXT_DATA));
        new_com_node->comment.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
        if (new_com_node->comment.data_sub_block_buffer.header == NULL)
        {
            free(new_com_node);
            return FALSE;
        }
        GIF_DATA_SUB_BLOCK_NODE *p = new_com_node->comment.data_sub_block_buffer.header;
        p->next = NULL;

        new_com_node->comment.header.introducer = 0x21;
        new_com_node->comment.header.label = 0xFE;

        for (;;)
        {
            int size = _GIFReaderByte(r);
            if (size < 0)
            {
                printf("_HandleExtension[0xFE]: truncated data sub-block.\n");
                free(p);
                free(new_com_node);
                return FALSE;
            }
            if (size == 0)
            {
                break;
            }
            GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
            if (new_node == NULL)
            {
                free(p);
                free(new_com_node);
                return FALSE;
            }
            new_node->data_size = (UINT8)size;
            new_node->next = NULL;
            if (!_GIFReaderCopy(r, new_node->data, (UINTN)size))
            {
                printf("_HandleExtension[0xFE]: truncated data sub-block payload.\n");
                free(new_node);
                free(p);
                free(new_com_node);
                return FALSE;
            }
            new_com_node->comment.data_sub_block_buffer.total_data_size += (UINTN)size;
            p->next = new_node;
            p = new_node;
        }

        new_com_node->comment.terminator = 0;
        gTailerPointer.comment->next = new_com_node;
        gTailerPointer.comment = new_com_node;
        _RecordComponentOrder(kCommentExt, gif);
        return TRUE;
    }

    case 0xF9: // Graphic Control Extension - 8 bytes: 21 F9 size packed delayLo delayHi tindex 00
    {
        if (gTailerPointer.graphics == NULL || gTailerPointer.graphics->next != NULL)
        {
            printf("gTailerPointer.graphics error\n");
            return FALSE;
        }

        byte = _GIFReaderByte(r); // block size, must be 4
        if (byte < 0)
        {
            printf("_HandleExtension[0xF9]: truncated graphic control extension.\n");
            return FALSE;
        }
        if (byte != 4)
        {
            printf("_HandleExtension[0xF9]: block size %d, expected 4; stopping.\n", byte);
            return FALSE;
        }

        // payload: packed, delay lo, delay hi, transparent index, terminator
        UINT8 payload[5];
        if (!_GIFReaderCopy(r, payload, sizeof(payload)))
        {
            printf("_HandleExtension[0xF9]: truncated graphic control extension payload.\n");
            return FALSE;
        }

        GIF_GRAPHICS_EXT_DATA *new_graphics_node = (GIF_GRAPHICS_EXT_DATA *)malloc(sizeof(GIF_GRAPHICS_EXT_DATA));
        if (new_graphics_node == NULL)
        {
            return FALSE;
        }
        memset(new_graphics_node, 0, sizeof(GIF_GRAPHICS_EXT_DATA));

        GIF_GRAPHICS_CONTROL_EXTENSION *g = &new_graphics_node->graphics;
        UINT8 packed = payload[0];
        g->header.introducer = 0x21;
        g->header.label = 0xF9;
        g->size = 4;
        g->flag_transparency_used = packed & 0x01;
        g->flag_input = (packed >> 1) & 0x01;
        g->flag_disposal_method = (packed >> 2) & 0x07;
        g->flag_reserved = (packed >> 5) & 0x07;
        g->delay_time = (UINT16)((UINT16)payload[1] | ((UINT16)payload[2] << 8));
        g->transparent_color_index = payload[3];
        g->terminator = payload[4];

        gTailerPointer.graphics->next = new_graphics_node;
        gTailerPointer.graphics = new_graphics_node;
        _RecordComponentOrder(kGraphicsExt, gif);

        return TRUE;
    }

    default:
    {
        // Unknown extension label: we cannot interpret it, but the generic block
        // grammar (label + data sub-block chain + 0x00) still holds, so skip it
        // instead of walking off into the payload bytes.
        if (!_SkipDataSubBlocks(r))
        {
            printf("_HandleExtension[0x%02X]: truncated unknown extension.\n", (unsigned)(UINT8)label);
            return FALSE;
        }
        return TRUE;
    }
    }
}

BOOL _HandleImageData(IN OUT _GIF_READER *r, OUT GIF **gif)
{
    if (gTailerPointer.image == NULL || gTailerPointer.image->next != NULL)
    {
        printf("gTailerPointer.image error\n");
        return FALSE;
    }

    GIF_IMAGE_DATA *new_image_node = (GIF_IMAGE_DATA *)malloc(sizeof(GIF_IMAGE_DATA));
    if (new_image_node == NULL)
    {
        return FALSE;
    }
    memset(new_image_node, 0, sizeof(GIF_IMAGE_DATA));
    new_image_node->one_frame_data.data_sub_block_buffer.header = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
    if (new_image_node->one_frame_data.data_sub_block_buffer.header == NULL)
    {
        free(new_image_node);
        return FALSE;
    }
    GIF_DATA_SUB_BLOCK_NODE *p = new_image_node->one_frame_data.data_sub_block_buffer.header;
    p->next = NULL;

    // Image descriptor body: the 0x2C introducer was consumed by the caller, so
    // 9 bytes remain (left, top, width, height, packed). The packed byte is made
    // of bit-fields, so the fields are unpacked explicitly.
    UINT8 descriptor[9];
    if (!_GIFReaderCopy(r, descriptor, sizeof(descriptor)))
    {
        printf("_HandleImageData: truncated image descriptor.\n");
        free(p);
        free(new_image_node);
        return FALSE;
    }
    {
        GIF_IMAGE_DESCRIPTOR *d = &new_image_node->image_descriptor;
        d->introducer = 0x2C;
        d->left = (UINT16)(descriptor[0] | (descriptor[1] << 8));
        d->top = (UINT16)(descriptor[2] | (descriptor[3] << 8));
        d->width = (UINT16)(descriptor[4] | (descriptor[5] << 8));
        d->height = (UINT16)(descriptor[6] | (descriptor[7] << 8));
        UINT8 packed = descriptor[8];
        d->flag_table_size = packed & 0x07;
        d->flag_sort = (packed >> 5) & 0x01;
        d->flag_interlace = (packed >> 6) & 0x01;
        d->flag_color_table = (packed >> 7) & 0x01;
        d->flag_reserved = 0;
    }

    if (new_image_node->image_descriptor.flag_color_table == 1)
    { // local color table
        UINT16 local_color_table_amount = 1 << (new_image_node->image_descriptor.flag_table_size + 1);
        new_image_node->local_color_table = (GIF_COLOR_TABLE *)malloc(sizeof(GIF_COLOR_TABLE) * local_color_table_amount);
        if (new_image_node->local_color_table == NULL ||
            !_GIFReaderCopy(r, new_image_node->local_color_table, sizeof(GIF_COLOR_TABLE) * local_color_table_amount))
        {
            printf("_HandleImageData: truncated local color table.\n");
            free(new_image_node->local_color_table);
            free(p);
            free(new_image_node);
            return FALSE;
        }
    }

    int size = _GIFReaderByte(r); // LZW_Minimum_Code
    if (size < 0)
    {
        printf("_HandleImageData: missing LZW minimum code size.\n");
        free(new_image_node->local_color_table);
        free(p);
        free(new_image_node);
        return FALSE;
    }
    new_image_node->one_frame_data.LZW_Minimum_Code = (UINT8)size;

    for (;;)
    { // one frame data: data_sub_block_buffer
        size = _GIFReaderByte(r);
        if (size < 0)
        {
            printf("_HandleImageData: truncated image data sub-block.\n");
            free(new_image_node->local_color_table);
            free(p);
            free(new_image_node);
            return FALSE;
        }
        if (size == 0)
        {
            break; // terminator
        }
        GIF_DATA_SUB_BLOCK_NODE *new_node = (GIF_DATA_SUB_BLOCK_NODE *)malloc(sizeof(GIF_DATA_SUB_BLOCK_NODE));
        if (new_node == NULL)
        {
            free(new_image_node->local_color_table);
            free(p);
            free(new_image_node);
            return FALSE;
        }
        new_node->next = NULL;
        new_node->data_size = (UINT8)size;
        if (!_GIFReaderCopy(r, new_node->data, (UINTN)size))
        {
            printf("_HandleImageData: truncated image data payload.\n");
            free(new_node);
            free(new_image_node->local_color_table);
            free(p);
            free(new_image_node);
            return FALSE;
        }
        new_image_node->one_frame_data.data_sub_block_buffer.total_data_size += (UINTN)size;
        p->next = new_node;
        p = new_node;
    }

    new_image_node->one_frame_data.terminator = 0;
    gTailerPointer.image->next = new_image_node;
    gTailerPointer.image = new_image_node;
    ++((*gif)->FramesCount);
    _RecordComponentOrder(kImageData, gif);

    return TRUE;
}

VOID _RecordComponentOrder(IN GIF_COMPONENT key, OUT GIF **gif)
{
    if (gAllocComponentCount * ALLOC_COMPONENT_AMOUNT - (*gif)->ComponentOrder.size < 10)
    {
        ++gAllocComponentCount;
        (*gif)->ComponentOrder.component = (GIF_COMPONENT *)realloc((*gif)->ComponentOrder.component, gAllocComponentCount * ALLOC_COMPONENT_AMOUNT * sizeof(GIF_COMPONENT));
    }
    (*gif)->ComponentOrder.component[(*gif)->ComponentOrder.size] = key;
    ++((*gif)->ComponentOrder.size);
}

UINTN _GetFileSizeByByte(IN FILE *fp)
{
    UINTN file_size = 0;
    if (fp == NULL)
    {
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);

    return file_size;
}

VOID _PrintBuffer(IN CHAR *buffer, IN UINTN len)
{
    FILE *fp = fopen("log", "ab");
    if (fp == NULL)
    {
        return;
    }
    for (UINTN i = 0; i < len; ++i)
    {
        fwrite(buffer + i, 1, 1, fp);
    }
    fclose(fp);
}
