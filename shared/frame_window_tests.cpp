#include "irb_frame_window.h"
#include <cstdlib>
#include <iostream>
static void check(bool ok) { if(!ok) { std::cerr<<"frame credit invariant failed\n"; std::exit(1); } }
int main() {
    irb_frame_window window{};
    for(unsigned limit:{0u,1u,2u,3u,4u}) {
        irb_window_init(&window,limit);
        check(window.limit==(limit>=1&&limit<=3?limit:1u));
        check(!irb_window_push(&window,0,100)&&!irb_window_push(&window,101,100));
        check(irb_window_push(&window,10,20));
        check(!irb_window_push(&window,10,21)&&!irb_window_ack(&window,9));
        for(unsigned i=1;i<window.limit;++i) check(irb_window_push(&window,10+i,20+i));
        check(!irb_window_available(&window)&&!irb_window_push(&window,20,30));
        if(window.limit>1) check(!irb_window_ack(&window,11)&&window.count==window.limit&&irb_window_oldest(&window)==20);
        check(irb_window_ack(&window,10)&&!irb_window_ack(&window,10));
        check(irb_window_push(&window,20,30));
        for(unsigned i=1;i<window.limit;++i) check(irb_window_oldest(&window)==20+i&&irb_window_ack(&window,10+i));
        check(irb_window_oldest(&window)==30&&irb_window_ack(&window,20));
        check(!window.count&&!irb_window_oldest(&window)&&!irb_window_ack(&window,20));
        check(!irb_window_push(&window,11,30));
    }
    std::cout<<"one/two/three-frame bounds, ordered ACKs, stale/future timestamps and slot reuse passed\n";
}
