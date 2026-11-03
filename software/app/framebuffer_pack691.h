#ifndef FRAMEBUFFER_PACK691_H
#define FRAMEBUFFER_PACK691_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* The caller owns validated, non-overlapping RGB and framebuffer mappings.
 * Byte encoding is identical to the legacy loop; no alignment/endian casts.
 * Stack storage is bounded to one 1920x32bpp row, no allocation in capture. */
static inline int fb691_copy(uint8_t *dst,size_t bytes,size_t stride,
 unsigned xoff,unsigned yoff,unsigned w,unsigned h,unsigned bpp,
 const uint32_t *rgb,size_t pixels,const uint32_t *red,const uint32_t *green,
 const uint32_t *blue,uint32_t alpha) {
 if(!dst || !rgb || !red || !green || !blue || !w || !h || w>1920 || h>1200 ||
    bpp<2 || bpp>4 || !stride || pixels<(size_t)w*h || xoff>SIZE_MAX/bpp) return -1;
 size_t x=(size_t)xoff*bpp,row=(size_t)w*bpp;
 size_t height_minus_one=(size_t)h-1;
 if(x>SIZE_MAX-row || x+row>stride || (size_t)yoff>SIZE_MAX-height_minus_one) return -1;
 size_t last=(size_t)yoff+h-1u,end=x+row;
 if(bytes<end || last>(bytes-end)/stride)return -1;
 uint8_t packed[1920*4];
 for(unsigned y=0;y<h;y++) {
  for(unsigned i=0;i<w;i++) {
   uint32_t c=rgb[(size_t)y*w+i];
   uint32_t p=red[(c>>16)&255]|green[(c>>8)&255]|blue[c&255]|alpha;
   for(unsigned k=0;k<bpp;k++)packed[(size_t)i*bpp+k]=(uint8_t)(p>>(8*k));
  }
  memcpy(dst+((size_t)yoff+y)*stride+x,packed,row);
 }
 return 0;
}
#endif
