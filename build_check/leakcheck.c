/* Allocation balance harness.
 *
 * Link it so the library's own object files call the counting versions (see the
 * `audit` target in the Makefile, or run `make check`):
 *
 *   gcc build_check/leakcheck.c gif_parser.c gif_canvas.c <the four lzw sources>
 *       -I"./" -no-pie -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
 *       -Wl,--wrap=free -o build_check/leakcheck.exe
 *
 * It reports the live block count at four points, and passes only if:
 *   - a full playback (compose -> output -> disposal for every frame, plus one
 *     BMP serialisation) allocates nothing that stays alive,
 *   - destroying the canvas returns to the post-decode count,
 *   - clearing the animation returns to zero.
 * A leak that no pixel comparison can see - the per-frame index buffer that used
 * to be overwritten instead of freed - is exactly what this catches.
 */
#include <stdio.h>
#include <stdlib.h>
#include "gif_parser.h"

static long live = 0;
static long peak = 0;
static long total = 0;

/* Track every live block so the survivors can be named at the end: "3 blocks
   leaked" is a hint, "3 blocks of 6 bytes, 72 bytes and 72 bytes" is a lead. */
#define TRACK_BITS 18
#define TRACK_SIZE (1u << TRACK_BITS)
typedef struct
{
    void *ptr;
    size_t size;
    void *caller;
} TRACK;
static TRACK track[TRACK_SIZE];
#define TOMBSTONE ((void *)1)

static unsigned track_slot(void *p)
{
    unsigned h = (unsigned)(((size_t)p >> 4) * 2654435761u);
    return h & (TRACK_SIZE - 1);
}

static void track_add(void *p, size_t size, void *caller)
{
    unsigned i = track_slot(p);
    while (track[i].ptr != NULL && track[i].ptr != TOMBSTONE)
    {
        i = (i + 1) & (TRACK_SIZE - 1);
    }
    track[i].ptr = p;
    track[i].size = size;
    track[i].caller = caller;
}

static void track_remove(void *p)
{
    unsigned i = track_slot(p);
    while (track[i].ptr != NULL)
    {
        if (track[i].ptr == p)
        {
            track[i].ptr = TOMBSTONE;
            return;
        }
        i = (i + 1) & (TRACK_SIZE - 1);
    }
}

static void track_dump(void)
{
    printf("  blocks still live:\n");
    for (unsigned i = 0; i < TRACK_SIZE; ++i)
    {
        if (track[i].ptr != NULL && track[i].ptr != TOMBSTONE)
        {
            printf("    %p  %lu byte(s)  from %p\n", track[i].ptr,
                   (unsigned long)track[i].size, track[i].caller);
        }
    }
}

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *ptr, size_t size);
void __real_free(void *ptr);

static void account(void)
{
    if (live > peak)
    {
        peak = live;
    }
}

void *__wrap_malloc(size_t size)
{
    void *p = __real_malloc(size);
    if (p != NULL)
    {
        ++live;
        ++total;
        track_add(p, size, __builtin_return_address(0));
        account();
    }
    return p;
}

void *__wrap_calloc(size_t count, size_t size)
{
    void *p = __real_calloc(count, size);
    if (p != NULL)
    {
        ++live;
        ++total;
        track_add(p, count * size, __builtin_return_address(0));
        account();
    }
    return p;
}

void *__wrap_realloc(void *ptr, size_t size)
{
    void *p = __real_realloc(ptr, size);
    if (p != NULL && ptr == NULL)
    {
        /* realloc(NULL, n) is an allocation; a move is 1 alloc + 1 free */
        ++live;
        ++total;
        track_add(p, size, __builtin_return_address(0));
        account();
    }
    else if (p != NULL)
    {
        track_remove(ptr);
        track_add(p, size, __builtin_return_address(0));
    }
    return p;
}

void __wrap_free(void *ptr)
{
    if (ptr != NULL)
    {
        --live;
        track_remove(ptr);
    }
    __real_free(ptr);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        return 2;
    }
    /* --wrap applies to everything statically linked, including the C runtime, so
       the argv/environment copies the startup code makes are already live here.
       They are the baseline, not a leak. */
    long crt_blocks = live;

    IMG_ANIMATION *animation = NULL;
    if (!GIFParserGetAnimationFromFile(argv[1], &animation))
    {
        /* A rejected file must leave nothing behind either. */
        long left = live - crt_blocks;
        printf("%-22s decode REJECTED  clear_left=%3ld allocs=%7ld  %s\n",
               argv[1], left, total, (left == 0) ? "OK" : "LEAK");
        if (left != 0)
        {
            track_dump();
        }
        printf("VERDICT %s %s\n", (left == 0) ? "OK" : "LEAK", argv[1]);
        return (left == 0) ? 0 : 1;
    }
    long after_decode = live - crt_blocks;
    /* Read the frame count while the animation is still alive: the summary is
       printed after GIFParserClearAnimation, and reading through a freed pointer
       is exactly the kind of mistake this harness exists to catch. */
    UINTN frame_count = animation->count;

    GIF_CANVAS canvas;
    if (!GIFCanvasCreate(&canvas, animation))
    {
        printf("%-22s canvas FAILED\n", argv[1]);
        GIFParserClearAnimation(animation);
        return 1;
    }
    IMG_FRAME *out = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * animation->width * animation->height);
    long baseline = live;

    /* Two passes: the first may allocate the lazily created disposal-3 snapshot,
       the second must allocate nothing at all. A per-frame leak would show up in
       the second pass multiplied by the frame count. */
    long after_first_pass = baseline;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (UINTN i = 0; i < animation->count; ++i)
        {
            GIFCanvasCompose(&canvas, animation, i);
            GIFCanvasOutput(&canvas, animation, out);
            GIFCanvasApplyDisposal(&canvas, animation, i);
            if (pass == 0 && i == 0)
            {
                UINTN bmp_size = 0;
                UINT8 *bmp = GIFParserAnimationFrameBMP(animation, i, &bmp_size);
                free(bmp); // the caller owns it
            }
        }
        if (pass == 0)
        {
            after_first_pass = live;
        }
    }
    long after_playback = live;

    free(out);
    GIFCanvasDestroy(&canvas);
    long after_canvas_destroy = live;
    GIFParserClearAnimation(animation);
    long after_clear = live;

    long second_pass_leak = after_playback - after_first_pass;
    long snapshot_blocks = after_first_pass - baseline;
    long leaked_by_canvas = after_canvas_destroy - after_decode - crt_blocks;
    long leaked_after_clear = after_clear - crt_blocks;
    int ok = (second_pass_leak == 0 && leaked_by_canvas == 0 && leaked_after_clear == 0 &&
              after_decode > 0);

    printf("%-22s frames=%4lu decode=%5ld pass2_leak=%3ld snapshot=%2ld canvas_leak=%3ld "
           "clear_left=%3ld allocs=%7ld peak_live=%4ld  %s\n",
           argv[1], (unsigned long)frame_count, after_decode, second_pass_leak,
           snapshot_blocks, leaked_by_canvas, leaked_after_clear, total, peak - crt_blocks,
           ok ? "OK" : "LEAK");
    if (after_clear != crt_blocks)
    {
        track_dump();
    }
    printf("VERDICT %s %s\n", ok ? "OK" : "LEAK", argv[1]);
    return ok ? 0 : 1;
}
