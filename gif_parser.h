#ifndef _GIF_PARSER_
#define _GIF_PARSER_

#include <stdbool.h>
#include "gif.h"
#include "bmp.h"

/* ---------------------------------------------------------------------------
 * Index-plus-composite animation model.
 *
 * Decoding a GIF used to materialise a full-canvas RGB frame per image, which
 * costs width*height*3 bytes for every frame (a 400x400 animation with 3200
 * frames needed ~1.5 GB). Nothing about the format requires that: a frame only
 * ever carries the pixels of its own rectangle, and everything outside it is
 * inherited from the canvas.
 *
 * So a decoded animation keeps only:
 *   - the indexed pixels of each frame's rectangle (1 byte per pixel), and
 *   - the few bytes of metadata needed to place them;
 * and the caller composites on demand into a canvas the library owns. Note that
 * decoding is NOT streamed: every frame is decoded up front. What is deferred is
 * the compositing, which is why the stored size is the sum of the rectangles
 * rather than frames * canvas. Producing a frame is one three-step sequence:
 *
 *     GIFCanvasCompose -> GIFCanvasOutput -> GIFCanvasApplyDisposal
 * ------------------------------------------------------------------------- */

/* One rendered pixel, in RGB field order, matching the GIF colour table. The BMP
 * writer reorders to BGR (and flips the row order) when it serialises. */
typedef GIF_COLOR_TABLE IMG_FRAME;

/* One image block, in the index domain. `pixels` is row-major within the
 * rectangle and stays in *file order*: for an interlaced frame the rows are the
 * four interlace passes and must be re-ordered when compositing. */
typedef struct
{
    UINT8 *pixels;
    UINTN left;
    UINTN top;
    UINTN width;
    UINTN height;
    BOOL interlaced;
    BOOL has_transparency;
    UINT8 transparent_index;
    UINT8 disposal_method;
    UINT32 delay_ms;
    GIF_COLOR_TABLE *palette; // the frame's own table, or the global one
    UINTN palette_entries;
} GIF_FRAME_INFO;

typedef struct
{
    UINTN width;
    UINTN height;
    UINTN count;
    GIF_FRAME_INFO *frames;
    /* Owned copy of the global colour table (NULL when the file has none). It is
       copied rather than borrowed because GIFParserGetAnimationFromFile frees
       the parsed GIF before returning, and frames reference it to render. */
    GIF_COLOR_TABLE *global_palette;
    UINTN global_palette_entries;
    UINT8 background_index;
} IMG_ANIMATION;

/* A rectangle of the canvas, in pixels. */
typedef struct
{
    UINTN left;
    UINTN top;
    UINTN width;
    UINTN height;
} GIF_RECT;

/* Composite state. Two parallel views of the same picture are kept:
 *
 *   rgb        - what each pixel currently *looks like*, and the only view any
 *                output path reads. A pixel carried over from an earlier frame
 *                keeps the colour that frame gave it, and that colour cannot be
 *                recovered from (index, current palette): the index was encoded
 *                against a different table, and GIFs routinely give frames their
 *                own (8yori.gif maps index 0 to (0,0,112) locally and
 *                (115,246,18) globally).
 *   index      - the same canvas in the index domain. Disposal 3 snapshots and
 *                restores it alongside rgb. Note that the transparency test uses
 *                the *incoming frame's* index, and the disposal-2 fill writes the
 *                background index without reading the canvas, so every read of
 *                `index` in this library is its own disposal-3 snapshot: no output
 *                path consumes it today.
 *
 * `saved` holds pre-draw snapshots of both, for disposal method 3.
 */
typedef struct
{
    UINTN width;
    UINTN height;
    UINT8 *index;
    IMG_FRAME *rgb;
    UINT8 *saved; // index snapshot followed by an IMG_FRAME snapshot
    /* rectangle of the frame GIFCanvasCompose drew last, so that
       GIFCanvasApplyDisposal knows what to restore */
    UINTN pending_width;
    UINTN pending_height;
} GIF_CANVAS;

BOOL GIFParserGetGifDataFromFile(IN const CHAR *filename, OUT GIF **gif, OUT UINTN *buffer_size);
UINT8 *GIFParserGetDataBufferFromGif(IN GIF *gif, IN UINTN buffer_size);
BOOL GIFParserClear(IN GIF *gif);

BOOL GIFParserGetAnimationFromFile(IN const CHAR *filename, OUT IMG_ANIMATION **animation);
BOOL GIFParserGetAnimationFromGif(IN GIF *gif, OUT IMG_ANIMATION **animation);
BOOL GIFParserClearAnimation(IN IMG_ANIMATION *animation);

/* canvas lifecycle (width/height are taken from the animation) */
BOOL GIFCanvasCreate(OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation);
VOID GIFCanvasDestroy(IN OUT GIF_CANVAS *canvas);
VOID GIFCanvasReset(IN OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation);

/* Layer frame `index` onto the canvas. Frames must be composed in order; this is
 * what applies transparency and prepares the disposal. It does NOT apply the
 * disposal yet: the caller must first read the picture out, because disposal
 * changes the canvas the *next* frame starts from. The intended order is
 *
 *     GIFCanvasCompose -> GIFCanvasOutput -> GIFCanvasApplyDisposal
 *
 * or, for a caller that only wants the finished picture, the single call
 * GIFCanvasDrawFrame below. */
BOOL GIFCanvasCompose(IN OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, IN UINTN index);

/* Copy the composited picture out. Call after GIFCanvasCompose and before
   GIFCanvasApplyDisposal. */
VOID GIFCanvasOutput(IN const GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, OUT IMG_FRAME *dst);

/* The part of the screen that can differ between frame `index` and the frame
 * displayed before it: the frame's own rectangle, union the previous frame's
 * rectangle when that frame's disposal rewrites pixels (methods 2 and 3 - 0 and 1
 * leave the canvas alone). Frame 0 reports the whole canvas because nothing has
 * been displayed yet.
 *
 * This is exact, not a heuristic: pixels outside the union cannot have changed,
 * so a caller that keeps an incrementally updated screen can copy/upload just
 * this region and stay pixel-identical to a full-canvas output. Returns FALSE
 * when there is nothing to update. */
BOOL GIFCanvasDirtyRect(IN const IMG_ANIMATION *animation, IN UINTN index, OUT GIF_RECT *rect);

/* Copy one rectangle of the canvas out, with the destination rows `dst_pitch`
 * pixels apart and `dst` pointing at the region's top-left. Pair it with
 * GIFCanvasDirtyRect to update a persistent screen buffer without touching the
 * rest of it. */
VOID GIFCanvasOutputRect(IN const GIF_CANVAS *canvas, OUT IMG_FRAME *dst, IN UINTN dst_pitch,
                         IN const GIF_RECT *rect);

/* Apply the disposal method of the frame last passed to GIFCanvasCompose. */
VOID GIFCanvasApplyDisposal(IN OUT GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, IN UINTN index);

/* Convert the composited canvas into `dst` (requires width*height IMG_FRAME).
   Alias of GIFCanvasOutput, kept for callers that only want the picture. */
VOID GIFCanvasRender(IN const GIF_CANVAS *canvas, IN const IMG_ANIMATION *animation, OUT IMG_FRAME *dst);

/* Compose + render in one call, starting from a freshly reset canvas. This is
 * the slow path: it replays frames 0..index to reach the requested state. */
BOOL GIFParserAnimationComposeFrame(IN IMG_ANIMATION *animation, IN UINTN index, OUT IMG_FRAME *dst);

/* Same, but writes a 24-bit BMP (54 byte header + bottom-up BGR rows) into a
 * buffer the caller must free. On success *frame_size receives its size. */
UINT8 *GIFParserAnimationFrameBMP(IN IMG_ANIMATION *animation, IN UINTN index, OUT UINTN *frame_size);

#endif
