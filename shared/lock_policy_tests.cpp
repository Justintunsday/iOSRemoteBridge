#include "irb_lock_policy.h"
#include <cstdlib>
#include <iostream>
static void require(bool ok) { if(!ok) std::abort(); }
int main() {
    for(int control=0;control<2;++control) for(int locked=0;locked<2;++locked)
    for(int negotiated=0;negotiated<2;++negotiated) for(int epoch=0;epoch<2;++epoch)
        require(!!irb_input_permitted(control,locked,negotiated,epoch)==
                !!(control&&!locked&&(!negotiated||epoch)));
    require(!irb_operation_is_input("wake_screen")&&!irb_operation_is_input("configure"));
    require(irb_operation_is_input("touch")&&irb_operation_is_input("key_event")&&irb_operation_is_input(nullptr));
    require(irb_capture_rate(60,1,1,1)==5&&irb_capture_rate(60,0,1,1)==60);
    require(irb_capture_rate(60,1,1,0)==0&&irb_capture_rate(30,0,0,0)==30);
    require(!irb_frame_visible(0,0)&&!irb_frame_visible(99,100)&&irb_frame_visible(100,100));
    std::cout<<"lock, control, legacy/negotiated epochs, screen pacing and frame barriers passed\n";
}
