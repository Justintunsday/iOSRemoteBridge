#include "irb_frame_window.h"
#include <cstdlib>
#include <iostream>
static void check(bool ok) { if(!ok) { std::cerr<<"frame credit invariant failed\n"; std::exit(1); } }
int main() {
    irb_frame_window window{};
    for(unsigned limit:{0u,1u,2u,3u}) {
        irb_window_init(&window,limit);
        check(window.limit==(limit==2?2u:1u));
        check(!irb_window_push(&window,0,100)&&!irb_window_push(&window,101,100));
        check(irb_window_push(&window,10,20));
        check(!irb_window_push(&window,10,21)&&!irb_window_ack(&window,9));
        if(window.limit==2) {
            check(irb_window_push(&window,11,21));
            check(!irb_window_available(&window)&&!irb_window_push(&window,12,22));
            check(!irb_window_ack(&window,11)&&window.count==2&&irb_window_oldest(&window)==20);
        } else check(!irb_window_push(&window,11,21));
        check(irb_window_ack(&window,10)&&!irb_window_ack(&window,10));
        check(irb_window_push(&window,12,22));
        if(window.limit==2) check(irb_window_oldest(&window)==21&&irb_window_ack(&window,11));
        check(irb_window_oldest(&window)==22&&irb_window_ack(&window,12));
        check(!window.count&&!irb_window_oldest(&window)&&!irb_window_ack(&window,12));
        check(!irb_window_push(&window,11,30));
    }
    std::cout<<"one/two-frame bounds, ordered ACKs, stale/future timestamps and slot reuse passed\n";
}
