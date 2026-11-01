#include "irb_hid_reports.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
static void check(bool ok) { if(!ok) { std::cerr<<"native HID report check failed\n"; std::exit(1); } }
int main() {
    // Parse the descriptor independently: input bit lengths must agree with
    // the report bytes sent to the kernel, and each collection must close.
    std::array<unsigned,4> bits{}; unsigned size=0,count=0,id=0,collections=0;
    for(size_t n=0;n<sizeof(irb_hid_descriptor);) {
        unsigned tag=irb_hid_descriptor[n++],bytes=tag&3; if(bytes==3) bytes=4;
        check(n+bytes<=sizeof(irb_hid_descriptor)); unsigned value=0;
        for(unsigned j=0;j<bytes;++j) value|=static_cast<unsigned>(irb_hid_descriptor[n++])<<(8*j);
        switch(tag&0xfc) {
        case 0x74:size=value;break; case 0x94:count=value;break;
        case 0x84:id=value;check(id>0&&id<bits.size());break;
        case 0x80:bits[id]+=size*count;break;
        case 0xa0:++collections;break; case 0xc0:check(collections>0);--collections;break;
        default:break;
        }
    }
    check(!collections&&bits[1]==64&&bits[2]==32&&bits[3]==48);
    uint8_t keys[256]{},key[9]{},position[5]{},mouse[7]{};
    keys[225]=1; keys[4]=1; keys[229]=1; keys[40]=1;
    check(irb_hid_keyboard(keys,key));
    check(key[0]==1&&key[1]==0x22&&key[2]==0&&key[3]==4&&key[4]==40&&key[8]==0);
    keys[4]=0;check(irb_hid_keyboard(keys,key)&&key[3]==40&&key[4]==0);
    for(unsigned i=4;i<11;++i) keys[i]=1;
    check(!irb_hid_keyboard(keys,key));
    memset(keys,0,sizeof(keys));check(irb_hid_keyboard(keys,key));
    for(unsigned i=1;i<9;++i) check(key[i]==0);
    check(irb_hid_position(0,1,position));
    check(position[0]==2&&position[1]==0&&position[2]==0&&position[3]==255&&position[4]==127);
    check(irb_hid_position(.5,.5,position)&&position[1]==0&&position[2]==64);
    check(!irb_hid_position(-.1,0,position)&&!irb_hid_position(0,1.1,position));
    check(!irb_hid_position(std::numeric_limits<double>::quiet_NaN(),0,position));
    irb_hid_mouse(3,-1,mouse); check(mouse[0]==3&&mouse[1]==3&&mouse[6]==255);
    for(unsigned i=2;i<6;++i) check(mouse[i]==0);
    irb_hid_mouse(0,0,mouse); for(unsigned i=1;i<7;++i) check(mouse[i]==0);
    std::cout<<"HID descriptor, modifiers, rollover, releases, absolute bounds and mouse reports passed\n";
}
