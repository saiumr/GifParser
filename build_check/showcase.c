#include <stdio.h>
#include <stdlib.h>
#include "gif_parser.h"
int main(void){
  IMG_ANIMATION *a=NULL;
  if(!GIFParserGetAnimationFromFile("build_check/cases/local_palette_probe.gif",&a)) return 1;
  GIF_FRAME_INFO *f=&a->frames[0];
  printf("canvas %lux%lu   frames %lu\n",(unsigned long)a->width,(unsigned long)a->height,(unsigned long)a->count);
  printf("frame0 rect=(%lu,%lu) size=%lux%lu disposal=%u transp=%d\n",
     (unsigned long)f->left,(unsigned long)f->top,(unsigned long)f->width,(unsigned long)f->height,
     (unsigned)f->disposal_method,(int)f->has_transparency);
  printf("frame pixels (indices, file order):");
  for(UINTN i=0;i<4;i++) printf(" %u", f->pixels[i]);
  printf("\n");
  printf("frame LCT:");
  for(UINTN i=0;i<f->palette_entries;i++) printf(" [%lu]=%u,%u,%u",(unsigned long)i,f->palette[i].r,f->palette[i].g,f->palette[i].b);
  printf("\n");
  printf("GCT bg[%u] = %u,%u,%u\n",(unsigned)a->background_index,
     a->global_palette[a->background_index].r,a->global_palette[a->background_index].g,a->global_palette[a->background_index].b);
  GIF_CANVAS c; GIFCanvasCreate(&c,a);
  IMG_FRAME *out=malloc(sizeof(IMG_FRAME)*a->width*a->height);
  GIFCanvasCompose(&c,a,0); GIFCanvasOutput(&c,a,out);
  printf("displayed frame (r,g,b):\n");
  for(UINTN y=0;y<a->height;y++){
    printf("  ");
    for(UINTN x=0;x<a->width;x++){ IMG_FRAME*p=&out[y*a->width+x]; printf("(%3u,%3u,%3u) ",p->r,p->g,p->b);} 
    printf("\n");
  }
  free(out); GIFCanvasDestroy(&c); GIFParserClearAnimation(a);
  return 0;
}
