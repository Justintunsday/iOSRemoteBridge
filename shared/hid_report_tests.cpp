#include "irb_hid_reports.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
static void check(bool ok) { if(!ok) { std::cerr<<"native HID report check failed\n"; std::exit(1); } }
int main() {
    // Parse each kernel device independently. Only relative axes may reach
    // the native mouse device; no absolute collection is registered.
    std::array<unsigned,4> bits{};
    for(unsigned device=0;device<IRB_HID_DEVICE_COUNT;++device) {
        size_t length=0; const uint8_t *descriptor=irb_hid_device_descriptor(device,&length);
        check(descriptor&&length); unsigned size=0,count=0,id=0,collections=0,applications=0,page=0,axes=0;
        unsigned absoluteAxes=0,relativeAxes=0;
        for(size_t n=0;n<length;) {
            unsigned tag=descriptor[n++],bytes=tag&3; if(bytes==3) bytes=4;
            check(n+bytes<=length); unsigned value=0;
            for(unsigned j=0;j<bytes;++j) value|=static_cast<unsigned>(descriptor[n++])<<(8*j);
            switch(tag&0xfc) {
            case 0x04:page=value;break;
            case 0x08:if(page==1&&(value==0x30||value==0x31)) ++axes;break;
            case 0x74:size=value;break; case 0x94:count=value;break;
            case 0x84:id=value;check(id==(device==IRB_HID_KEYBOARD?1u:3u)&&id<bits.size());break;
            case 0x80:
                bits[id]+=size*count;
                if(axes) { if(value&4) relativeAxes+=axes; else absoluteAxes+=axes; }
                axes=0;break;
            case 0xa0:if(!collections) { check(value==1);++applications; } ++collections;axes=0;break;
            case 0xc0:check(collections>0);--collections;axes=0;break;
            default:break;
            }
        }
        check(!collections&&applications==1);
        check(irb_hid_report_device(static_cast<uint8_t>(id))==static_cast<int>(device));
        if(device==IRB_HID_MOUSE) check(absoluteAxes==0&&relativeAxes==2);
    }
    check(bits[1]==64&&bits[2]==0&&bits[3]==48);
    size_t invalidLength=1;check(!irb_hid_device_descriptor(2,&invalidLength)&&!invalidLength);
    check(irb_hid_report_device(0)==-1&&irb_hid_report_device(2)==-1&&irb_hid_report_device(4)==-1);
    uint8_t keys[256]{},key[9]{},mouse[7]{};
    keys[225]=1; keys[4]=1; keys[229]=1; keys[40]=1;
    check(irb_hid_keyboard(keys,key));
    check(key[0]==1&&key[1]==0x22&&key[2]==0&&key[3]==4&&key[4]==40&&key[8]==0);
    keys[4]=0;check(irb_hid_keyboard(keys,key)&&key[3]==40&&key[4]==0);
    for(unsigned i=4;i<11;++i) keys[i]=1;
    check(!irb_hid_keyboard(keys,key));
    memset(keys,0,sizeof(keys));check(irb_hid_keyboard(keys,key));
    for(unsigned i=1;i<9;++i) check(key[i]==0);
    irb_hid_mouse(1234,-2345,3,-1,mouse);
    check(mouse[0]==3&&mouse[1]==3&&mouse[2]==0xd2&&mouse[3]==4&&mouse[4]==0xd7&&mouse[5]==0xf6&&mouse[6]==255);
    irb_hid_mouse(-32767,32767,0,127,mouse);
    check(mouse[2]==1&&mouse[3]==128&&mouse[4]==255&&mouse[5]==127&&mouse[6]==127);
    irb_hid_mouse(0,0,0,0,mouse); for(unsigned i=1;i<7;++i) check(mouse[i]==0);
    std::cout<<"HID descriptor, modifiers, rollover, releases, isolated relative mouse reports passed\n";
}
