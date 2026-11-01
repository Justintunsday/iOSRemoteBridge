#ifndef IRB_PROTOCOL_H
#define IRB_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
#define IRB_HEADER_SIZE 24u
#define IRB_MAX_PAYLOAD (8u * 1024u * 1024u)
#define IRB_MAX_CONTROL 65536u
enum { IRB_AUTH=1, IRB_CONTROL=2, IRB_RESPONSE=3, IRB_VIDEO=4,
       IRB_PING=5, IRB_PONG=6, IRB_START=128, IRB_STOP=129, IRB_CREDIT=130 };
typedef struct { uint8_t type, flags; uint32_t length, sequence; uint64_t pts_us; } irb_header;
static inline uint32_t irb_get32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static inline void irb_put32(uint8_t *p, uint32_t n) {
    p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n;
}
static inline void irb_pack(uint8_t p[IRB_HEADER_SIZE], irb_header h) {
    p[0]='I'; p[1]='R'; p[2]='B'; p[3]='1'; p[4]=1; p[5]=h.type; p[6]=h.flags; p[7]=0;
    irb_put32(p+8,h.length); irb_put32(p+12,h.sequence);
    irb_put32(p+16,(uint32_t)(h.pts_us>>32)); irb_put32(p+20,(uint32_t)h.pts_us);
}
static inline int irb_unpack(const uint8_t *p, size_t n, irb_header *h) {
    if(n<IRB_HEADER_SIZE || p[0]!='I'||p[1]!='R'||p[2]!='B'||p[3]!='1'||p[4]!=1||p[7]!=0) return 0;
    if(p[5]!=IRB_AUTH&&p[5]!=IRB_CONTROL&&p[5]!=IRB_RESPONSE&&p[5]!=IRB_VIDEO&&
       p[5]!=IRB_PING&&p[5]!=IRB_PONG&&p[5]!=IRB_START&&p[5]!=IRB_STOP&&p[5]!=IRB_CREDIT) return 0;
    h->type=p[5]; h->flags=p[6]; h->length=irb_get32(p+8); h->sequence=irb_get32(p+12);
    h->pts_us=((uint64_t)irb_get32(p+16)<<32)|irb_get32(p+20);
    if(h->flags & ~1u) return 0;
    if(h->type!=IRB_VIDEO && h->flags) return 0;
    return h->length<=IRB_MAX_PAYLOAD && (h->type==IRB_VIDEO||h->length<=IRB_MAX_CONTROL);
}
/* Displayed normalized coordinates -> natural portrait digitizer plane.
   Rotation is clockwise and explicitly chosen by the viewer. */
static inline int irb_unrotate(double x,double y,int rotation,double *ox,double *oy) {
    if(!isfinite(x)||!isfinite(y)||x<0||x>1||y<0||y>1) return 0;
    switch(rotation) {
      case 0: *ox=x; *oy=y; break;
      case 90: *ox=y; *oy=1-x; break;
      case 180: *ox=1-x; *oy=1-y; break;
      case 270: *ox=1-y; *oy=x; break;
      default: return 0;
    }
    return 1;
}
#ifdef __cplusplus
}
#endif
#endif
