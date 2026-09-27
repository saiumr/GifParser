/* Peak working set measurement harness for the streamed animation model.
 *
 * windows.h cannot be included next to gif.h (both define CHAR/BOOL), so the one
 * call we need is declared here directly. PROCESS_MEMORY_COUNTERS is a stable
 * ABI: 6 DWORDs, then SIZE_Ts.
 */
#include <stdio.h>
#include <stdlib.h>
#include "gif_parser.h"

typedef unsigned long DWORD_SIZE;
typedef unsigned long long SIZE_T_SIZE;

typedef struct
{
    DWORD_SIZE cb;
    DWORD_SIZE PageFaultCount;
    SIZE_T_SIZE PeakWorkingSetSize;
    SIZE_T_SIZE WorkingSetSize;
    SIZE_T_SIZE QuotaPeakPagedPoolUsage;
    SIZE_T_SIZE QuotaPagedPoolUsage;
    SIZE_T_SIZE QuotaPeakNonPagedPoolUsage;
    SIZE_T_SIZE QuotaNonPagedPoolUsage;
    SIZE_T_SIZE PagefileUsage;
    SIZE_T_SIZE PeakPagefileUsage;
} PMC_LOCAL;

__declspec(dllimport) void *__stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall GetProcessMemoryInfo(void *process, PMC_LOCAL *counters, DWORD_SIZE size);

static double peak_mb(void)
{
    PMC_LOCAL pmc;
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return -1.0;
    }
    return (double)pmc.PeakWorkingSetSize / 1048576.0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        return 2;
    }
    IMG_ANIMATION *a = NULL;
    if (!GIFParserGetAnimationFromFile(argv[1], &a))
    {
        printf("%-16s decode FAILED\n", argv[1]);
        return 1;
    }
    double after_decode = peak_mb();

    /* allocate exactly what a streaming player needs: one canvas + one frame */
    GIF_CANVAS canvas;
    if (!GIFCanvasCreate(&canvas, a))
    {
        printf("canvas failed\n");
        return 1;
    }
    IMG_FRAME *out = (IMG_FRAME *)malloc(sizeof(IMG_FRAME) * a->width * a->height);
    for (UINTN i = 0; i < a->count; ++i)
    {
        GIFCanvasCompose(&canvas, a, i);
        GIFCanvasOutput(&canvas, a, out);
        GIFCanvasApplyDisposal(&canvas, a, i);
    }
    double with_playback = peak_mb();

    printf("%-22s frames=%4lu %4lux%-4lu  decode_peak=%7.1f MB  playback_peak=%7.1f MB\n",
           argv[1], (unsigned long)a->count, (unsigned long)a->width, (unsigned long)a->height,
           after_decode, with_playback);

    free(out);
    GIFCanvasDestroy(&canvas);
    GIFParserClearAnimation(a);
    return 0;
}
