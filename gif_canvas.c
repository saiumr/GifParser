/* Canvas compositing for the streamed animation model.
 *
 * This is the half of the old gif_parser.c compositor that turns per-frame
 * rectangles into a picture. Keeping it in its own translation unit means the
 * parser no longer has to know anything about disposal methods or palettes, and
 * a player can composite frame-by-frame without ever holding more than one
 * canvas and one output frame.
 *
 * Three invariants drive the design:
 *
 * 1. Composition is driven by indices. A frame's indices only mean anything
 *    together with that frame's palette, and "leave this pixel alone" is a
 *    statement about canvas state.
 *
 * 2. The canvas keeps the composited *colour* of every pixel as well. A pixel
 *    carried over from an earlier frame must keep the colour that frame gave it,
 *    and that colour cannot be recovered from (index, current palette) - the
 *    index was encoded against a different table. GIFs routinely give frames
 *    their own table (8yori.gif maps index 0 to (0,0,112) locally and
 *    (115,246,18) globally), so re-rendering carried-over pixels through the
 *    current frame's table silently recolours the image.
 *
 * 3. disposal 3 restores the canvas to its pre-draw state, so both views are
 *    snapshotted together.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gif_parser.h"

static UINT8 _CanvasInitIndex(IN const IMG_ANIMATION *animation)
{
    return (animation->global_palette != NULL) ? animation->background_index : 0;
}

/* The colour the initial fill and the disposal-2 fill produce: the background
 * colour named by the Logical Screen Descriptor, through the global table. */
static IMG_FRAME _BackgroundColour(IN const IMG_ANIMATION *animation)
{
    IMG_FRAME colour = {0, 0, 0};
    UINT8 bg = _CanvasInitIndex(animation);
    if (animation->global_palette != NULL && bg < animation->global_palette_entries)
    {
        colour = animation->global_palette[bg];
    }
    return colour;
}

/* Build the display-row -> stored-row mapping for an interlaced frame: rows
 * arrive as passes 0,8,16.. / 4,12,.. / 2,6,.. / 1,3,.. */
static VOID _InterlaceRowOrder(IN OUT UINTN *order, IN UINTN height)
{
    for (UINTN row = 0; row < height; ++row)
    {
        order[row] = row; // identity fallback for rows no pass covers
    }
    UINTN n = 0;
    for (UINTN pass = 0; pass < 4 && n < height; ++pass)
    {
        UINTN start = 0;
        UINTN step = 8;
        switch (pass)
        {
        case 1:
            start = 4;
            break;
        case 2:
            start = 2;
            step = 4;
            break;
        case 3:
            start = 1;
            step = 2;
            break;
        default:
            break;
        }
        for (UINTN row = start; row < height && n < height; row += step)
        {
            order[row] = n++;
        }
    }
}

BOOL GIFCanvasCreate(IN OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation)
{
    if (canvas == NULL || animation == NULL || animation->width == 0 || animation->height == 0)
    {
        return FALSE;
    }
    UINTN n = animation->width * animation->height;
    canvas->width = animation->width;
    canvas->height = animation->height;
    canvas->saved = NULL;
    canvas->pending_width = 0;
    canvas->pending_height = 0;
    canvas->index = (UINT8 *)malloc(n);
    canvas->rgb = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * n);
    if (canvas->index == NULL || canvas->rgb == NULL)
    {
        free(canvas->index);
        free(canvas->rgb);
        canvas->index = NULL;
        canvas->rgb = NULL;
        return FALSE;
    }
    GIFCanvasReset(canvas, animation);
    return TRUE;
}

VOID GIFCanvasReset(IN OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation)
{
    if (canvas == NULL || canvas->index == NULL || animation == NULL)
    {
        return;
    }
    UINTN n = canvas->width * canvas->height;
    memset(canvas->index, _CanvasInitIndex(animation), n);
    IMG_FRAME fill = _BackgroundColour(animation);
    for (UINTN i = 0; i < n; ++i)
    {
        canvas->rgb[i] = fill;
    }
}

VOID GIFCanvasDestroy(IN OUT GIF_CANVAS *canvas)
{
    if (canvas == NULL)
    {
        return;
    }
    free(canvas->index);
    free(canvas->rgb);
    free(canvas->saved);
    canvas->index = NULL;
    canvas->rgb = NULL;
    canvas->saved = NULL;
}

/* Intersect a frame's rectangle with the canvas. Malformed files may place a
 * frame partly or wholly outside; everything downstream has to agree on the same
 * clipped rectangle or the incremental output would miss pixels. */
static GIF_RECT _ClipToCanvas(IN const GIF_FRAME_INFO *frame, IN UINTN canvas_width, IN UINTN canvas_height)
{
    GIF_RECT r;
    r.left = frame->left;
    r.top = frame->top;
    r.width = 0;
    r.height = 0;
    if (r.left < canvas_width)
    {
        UINTN available = canvas_width - r.left;
        r.width = (frame->width < available) ? frame->width : available;
    }
    if (r.top < canvas_height)
    {
        UINTN available = canvas_height - r.top;
        r.height = (frame->height < available) ? frame->height : available;
    }
    return r;
}

static GIF_RECT _UnionRect(IN GIF_RECT a, IN GIF_RECT b)
{
    GIF_RECT r = a;
    if (b.width == 0 || b.height == 0)
    {
        return r;
    }
    if (r.width == 0 || r.height == 0)
    {
        return b;
    }
    UINTN right = (a.left + a.width > b.left + b.width) ? a.left + a.width : b.left + b.width;
    UINTN bottom = (a.top + a.height > b.top + b.height) ? a.top + a.height : b.top + b.height;
    r.left = (a.left < b.left) ? a.left : b.left;
    r.top = (a.top < b.top) ? a.top : b.top;
    r.width = right - r.left;
    r.height = bottom - r.top;
    return r;
}

BOOL GIFCanvasDirtyRect(IN const IMG_ANIMATION *animation, IN UINTN index, OUT GIF_RECT *rect)
{
    if (animation == NULL || rect == NULL || index >= animation->count ||
        animation->width == 0 || animation->height == 0)
    {
        return FALSE;
    }

    /* Nothing has been drawn yet: whatever the destination holds, this frame has
       to put the whole canvas there. */
    if (index == 0)
    {
        rect->left = 0;
        rect->top = 0;
        rect->width = animation->width;
        rect->height = animation->height;
        return TRUE;
    }

    GIF_RECT dirty = _ClipToCanvas(&animation->frames[index], animation->width, animation->height);

    /* Disposal runs after the previous frame was shown, so the pixels it rewrote
       are part of what differs from the previous displayed picture. Methods 0 and
       1 leave the canvas untouched and therefore contribute nothing. */
    const GIF_FRAME_INFO *previous = &animation->frames[index - 1];
    if (previous->disposal_method == 0x02 || previous->disposal_method == 0x03)
    {
        dirty = _UnionRect(dirty, _ClipToCanvas(previous, animation->width, animation->height));
    }

    *rect = dirty;
    return dirty.width > 0 && dirty.height > 0;
}

VOID GIFCanvasOutputRect(IN const GIF_CANVAS *canvas, OUT IMG_FRAME *dst, IN UINTN dst_pitch,
                         IN const GIF_RECT *rect)
{
    if (canvas == NULL || canvas->rgb == NULL || dst == NULL || rect == NULL)
    {
        return;
    }
    UINTN left = rect->left;
    UINTN top = rect->top;
    UINTN width = rect->width;
    UINTN height = rect->height;
    if (left >= canvas->width || top >= canvas->height)
    {
        return;
    }
    UINTN available = canvas->width - left;
    if (width > available)
    {
        width = available;
    }
    available = canvas->height - top;
    if (height > available)
    {
        height = available;
    }
    for (UINTN row = 0; row < height; ++row)
    {
        memcpy(dst + row * dst_pitch,
               canvas->rgb + (top + row) * canvas->width + left,
               sizeof(IMG_FRAME) * width);
    }
}

BOOL GIFCanvasCompose(IN OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, IN UINTN index)
{
    if (canvas == NULL || animation == NULL || canvas->index == NULL || canvas->rgb == NULL)
    {
        return FALSE;
    }
    if (index >= animation->count)
    {
        return FALSE;
    }

    const GIF_FRAME_INFO *frame = &animation->frames[index];
    UINTN fw = frame->width;
    UINTN fh = frame->height;
    if (fw == 0 || fh == 0 || frame->pixels == NULL)
    {
        return TRUE; // empty frame: nothing to draw, the canvas already shows the background
    }

    UINTN *row_order = NULL;
    if (frame->interlaced && fh > 1)
    {
        row_order = (UINTN *)malloc(sizeof(UINTN) * fh);
        if (row_order == NULL)
        {
            return FALSE;
        }
        _InterlaceRowOrder(row_order, fh);
    }

    /* Clip the rectangle to the canvas: malformed files may exceed it. This is the
       same helper GIFCanvasDirtyRect uses, so what is drawn and what is reported as
       dirty cannot drift apart. */
    GIF_RECT draw = _ClipToCanvas(frame, canvas->width, canvas->height);
    UINTN left = draw.left;
    UINTN top = draw.top;
    UINTN draw_w = draw.width;
    UINTN draw_h = draw.height;

    /* Disposal 3 restores the rectangle as it was before this frame, so both views
       are snapshotted first. The buffer is sized for the whole canvas: the first
       disposal-3 frame is not necessarily the largest one (lm.gif frame 35 is
       316x313 while frame 36 is 329x316). */
    UINTN canvas_pixels = canvas->width * canvas->height;
    if (frame->disposal_method == 0x03 && draw_w > 0 && draw_h > 0)
    {
        if (canvas->saved == NULL)
        {
            canvas->saved = (UINT8 *)malloc(canvas_pixels + sizeof(IMG_FRAME) * canvas_pixels);
            if (canvas->saved == NULL)
            {
                free(row_order);
                return FALSE;
            }
        }
        IMG_FRAME *saved_rgb = (IMG_FRAME *)(canvas->saved + canvas_pixels);
        for (UINTN row = 0; row < draw_h; ++row)
        {
            UINTN offset = (top + row) * canvas->width + left;
            memcpy(canvas->saved + row * draw_w, canvas->index + offset, draw_w);
            memcpy(saved_rgb + row * draw_w, canvas->rgb + offset, sizeof(IMG_FRAME) * draw_w);
        }
    }

    /* Draw: transparent pixels keep both their index and their colour, everything
       else lands as the frame encodes it.

       Disposal is deliberately NOT applied here: it defines the canvas the next
       frame starts from, so the caller has to render this frame first. */
    for (UINTN row = 0; row < draw_h; ++row)
    {
        UINTN src_row = (row_order != NULL) ? row_order[row] : row;
        const UINT8 *src = frame->pixels + src_row * fw;
        UINTN dst_offset = (top + row) * canvas->width + left;
        UINT8 *dst_index = canvas->index + dst_offset;
        IMG_FRAME *dst_rgb = canvas->rgb + dst_offset;

        if (!frame->has_transparency)
        {
            memcpy(dst_index, src, draw_w);
            if (frame->palette != NULL)
            {
                for (UINTN col = 0; col < draw_w; ++col)
                {
                    UINT8 value = src[col];
                    dst_rgb[col] = (value < frame->palette_entries) ? frame->palette[value]
                                                                    : (IMG_FRAME){0, 0, 0};
                }
            }
            continue;
        }
        for (UINTN col = 0; col < draw_w; ++col)
        {
            UINT8 value = src[col];
            if (value == frame->transparent_index)
            {
                continue; // leave the canvas pixel exactly as it is
            }
            dst_index[col] = value;
            if (frame->palette != NULL && value < frame->palette_entries)
            {
                dst_rgb[col] = frame->palette[value];
            }
        }
    }
    free(row_order);


    /* Remember where this frame landed so the caller can render it and then ask
       for the disposal to be applied. */
    canvas->pending_width = draw_w;
    canvas->pending_height = draw_h;

    return TRUE;
}

/* Copy out the composited picture. Call after GIFCanvasCompose and before
   GIFCanvasApplyDisposal. */
VOID GIFCanvasOutput(IN const GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, OUT IMG_FRAME *dst)
{
    if (canvas == NULL || canvas->rgb == NULL || dst == NULL)
    {
        return;
    }
    (void)animation;
    memcpy(dst, canvas->rgb, sizeof(IMG_FRAME) * canvas->width * canvas->height);
}

/* Apply the disposal method of the frame GIFCanvasCompose last drew. */
VOID GIFCanvasApplyDisposal(IN OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, IN UINTN index)
{
    if (canvas == NULL || canvas->index == NULL || animation == NULL || index >= animation->count)
    {
        return;
    }
    const GIF_FRAME_INFO *frame = &animation->frames[index];
    UINTN draw_w = canvas->pending_width;
    UINTN draw_h = canvas->pending_height;
    if (draw_w == 0 || draw_h == 0)
    {
        return;
    }
    UINTN left = frame->left;
    UINTN top = frame->top;
    UINTN canvas_pixels = canvas->width * canvas->height;

    if (frame->disposal_method == 0x02)
    {
        /* "Restore to background colour": the colour named by the Logical Screen
           Descriptor background index, rendered through the global table that
           owns that index. It is deliberately NOT the frame's transparent index
           colour - the whole point of disposal 2 is that the next frame sees the
           untouched background through its transparent pixels. */
        UINT8 restore_index = _CanvasInitIndex(animation);
        IMG_FRAME restore_rgb = _BackgroundColour(animation);
        for (UINTN row = 0; row < draw_h; ++row)
        {
            UINTN offset = (top + row) * canvas->width + left;
            memset(canvas->index + offset, restore_index, draw_w);
            for (UINTN col = 0; col < draw_w; ++col)
            {
                canvas->rgb[offset + col] = restore_rgb;
            }
        }
    }
    else if (frame->disposal_method == 0x03 && canvas->saved != NULL)
    {
        IMG_FRAME *saved_rgb = (IMG_FRAME *)(canvas->saved + canvas_pixels);
        for (UINTN row = 0; row < draw_h; ++row)
        {
            UINTN offset = (top + row) * canvas->width + left;
            memcpy(canvas->index + offset, canvas->saved + row * draw_w, draw_w);
            memcpy(canvas->rgb + offset, saved_rgb + row * draw_w, sizeof(IMG_FRAME) * draw_w);
        }
    }
    /* disposal 0 (unspecified) and 1 (do not dispose) both keep the canvas */
    canvas->pending_width = 0;
    canvas->pending_height = 0;
}


VOID GIFCanvasRender(IN const GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, OUT IMG_FRAME *dst)
{
    GIFCanvasOutput(canvas, animation, dst);
}

BOOL GIFParserAnimationComposeFrame(IN IMG_ANIMATION *animation, IN UINTN index, OUT IMG_FRAME *dst)
{
    if (animation == NULL || dst == NULL || index >= animation->count)
    {
        return FALSE;
    }

    GIF_CANVAS canvas;
    if (!GIFCanvasCreate(&canvas, animation))
    {
        return FALSE;
    }
    BOOL ok = TRUE;
    for (UINTN i = 0; i <= index && ok; ++i)
    {
        ok = GIFCanvasCompose(&canvas, animation, i);
        if (ok && i == index)
        {
            GIFCanvasOutput(&canvas, animation, dst);
        }
        if (ok)
        {
            GIFCanvasApplyDisposal(&canvas, animation, i);
        }
    }
    GIFCanvasDestroy(&canvas);
    return ok;
}

UINT8 *GIFParserAnimationFrameBMP(IN IMG_ANIMATION *animation, IN UINTN index, OUT UINTN *frame_size)
{
    if (animation == NULL || frame_size == NULL || index >= animation->count)
    {
        return NULL;
    }

    /* row stride is the pixel row padded up to a 4 byte boundary */
    UINTN row_bytes = animation->width * 3;
    if (row_bytes % 4 != 0)
    {
        row_bytes += 4 - (row_bytes % 4);
    }
    UINTN data_size = row_bytes * animation->height;
    UINTN total = 54 + data_size;

    IMG_FRAME *rgb = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * animation->width * animation->height);
    if (rgb == NULL)
    {
        return NULL;
    }
    if (!GIFParserAnimationComposeFrame(animation, index, rgb))
    {
        free(rgb);
        return NULL;
    }

    UINT8 *buffer = (UINT8 *)malloc(total);
    if (buffer == NULL)
    {
        free(rgb);
        return NULL;
    }

    BMP_IMAGE_HEADER header;
    memset(&header, 0, sizeof(header));
    header.CharB = 'B';
    header.CharM = 'M';
    header.Size = (UINT32)total;
    header.ImageOffset = 0x36;
    header.InfoHeaderSize = 0x28;
    header.PixelWidth = (UINT32)animation->width;
    header.PixelHeight = (UINT32)animation->height;
    header.Planes = 1;
    header.BitPerPixel = 0x18; // 24 bits per pixel
    header.ImageSize = (UINT32)data_size;
    header.XPixelsPerMeter = 0x1625;
    header.YPixelsPerMeter = 0x1625;
    memcpy(buffer, &header, 54);

    /* BMP stores colours as BGR and its rows bottom-up: the first row in the file
       is the *bottom* row of the picture. Walking the source rows downwards and
       appending them in that order produces the required layout.
       Padding is computed from the bytes written *within the row*: `out` is an
       absolute file offset and the 54 byte header is not 4-byte aligned, so
       testing `out % 4` would pad by the wrong amount (it inserted 2 stray bytes
       at every row boundary and truncated the last row). */
    UINT8 *image = buffer + 54;
    UINTN image_bytes = 0;
    for (UINTN row = animation->height; row-- > 0;)
    {
        const IMG_FRAME *src = rgb + row * animation->width;
        for (UINTN col = 0; col < animation->width; ++col)
        {
            image[image_bytes++] = src[col].b;
            image[image_bytes++] = src[col].g;
            image[image_bytes++] = src[col].r;
        }
        while (image_bytes % 4 != 0)
        {
            image[image_bytes++] = 0;
        }
    }

    free(rgb);
    *frame_size = total;
    return buffer;
}
