#ifndef IRB_FRAME_WINDOW_H
#define IRB_FRAME_WINDOW_H
#include <stdint.h>

#define IRB_MAX_VIDEO_WINDOW 2u
typedef struct {
    unsigned limit,count;
    uint64_t pts[IRB_MAX_VIDEO_WINDOW],started[IRB_MAX_VIDEO_WINDOW],last_pts;
} irb_frame_window;

static inline void irb_window_init(irb_frame_window *window,unsigned limit) {
    window->count=0; window->last_pts=0;
    for(unsigned i=0;i<IRB_MAX_VIDEO_WINDOW;++i) { window->pts[i]=0; window->started[i]=0; }
    window->limit=limit==IRB_MAX_VIDEO_WINDOW?IRB_MAX_VIDEO_WINDOW:1;
}
static inline int irb_window_available(const irb_frame_window *window) {
    return window->count<window->limit;
}
static inline int irb_window_push(irb_frame_window *window,uint64_t pts,uint64_t now) {
    if(!irb_window_available(window)||!pts||pts>now||pts<=window->last_pts) return 0;
    window->pts[window->count]=pts; window->started[window->count]=now;
    ++window->count; window->last_pts=pts; return 1;
}
static inline int irb_window_ack(irb_frame_window *window,uint64_t pts) {
    if(!window->count||window->pts[0]!=pts) return 0;
    for(unsigned i=1;i<window->count;++i) {
        window->pts[i-1]=window->pts[i]; window->started[i-1]=window->started[i];
    }
    --window->count; return 1;
}
static inline uint64_t irb_window_oldest(const irb_frame_window *window) {
    return window->count?window->started[0]:0;
}
#endif
