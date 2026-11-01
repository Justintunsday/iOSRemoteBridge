#ifndef IRB_HID_REPORTS_H
#define IRB_HID_REPORTS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
/* Keep absolute and relative pointer collections on distinct kernel devices.
   The native iOS event driver must not merge their per-service pointer state.
   Each descriptor starts with fresh HID global state. */
enum { IRB_HID_KEYBOARD=0,IRB_HID_POSITION=1,IRB_HID_MOUSE=2,IRB_HID_DEVICE_COUNT=3 };
static const uint8_t irb_hid_keyboard_descriptor[]={
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x08,0x81,0x02,0x75,0x08,0x95,0x01,0x81,0x01,
    0x19,0x00,0x29,0xdf,0x15,0x00,0x25,0xdf,0x75,0x08,0x95,0x06,0x81,0x00,0xc0
};
static const uint8_t irb_hid_position_descriptor[]={
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x02,0x09,0x01,0xa1,0x00,
    0x09,0x30,0x09,0x31,0x15,0x00,0x26,0xff,0x7f,
    0x35,0x00,0x46,0xff,0x7f,
    0x75,0x10,0x95,0x02,0x81,0x02,0xc0,0xc0
};
static const uint8_t irb_hid_mouse_descriptor[]={
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x03,0x09,0x01,0xa1,0x00,
    0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x05,0x81,0x02,0x75,0x03,0x95,0x01,0x81,0x01,
    0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x01,0x80,0x26,0xff,0x7f,
    0x75,0x10,0x95,0x02,0x81,0x06,
    0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0
};
static inline const uint8_t *irb_hid_device_descriptor(unsigned device,size_t *length) {
    switch(device) {
    case IRB_HID_KEYBOARD:*length=sizeof(irb_hid_keyboard_descriptor);return irb_hid_keyboard_descriptor;
    case IRB_HID_POSITION:*length=sizeof(irb_hid_position_descriptor);return irb_hid_position_descriptor;
    case IRB_HID_MOUSE:*length=sizeof(irb_hid_mouse_descriptor);return irb_hid_mouse_descriptor;
    default:*length=0;return NULL;
    }
}
static inline int irb_hid_report_device(uint8_t report) {
    return report>=1&&report<=IRB_HID_DEVICE_COUNT?(int)report-1:-1;
}
static inline int irb_hid_keyboard(const uint8_t keys[256],uint8_t report[9]) {
    memset(report,0,9); report[0]=1; unsigned count=0;
    for(unsigned i=224;i<232;++i) if(keys[i]) report[1]|=(uint8_t)(1u<<(i-224));
    for(unsigned i=4;i<224;++i) if(keys[i]) {
        if(count==6) return 0; // Six non-modifier keys plus eight modifier bits.
        report[3+count++]=(uint8_t)i;
    }
    return 1;
}
static inline int irb_hid_position(double x,double y,uint8_t report[5]) {
    if(!isfinite(x)||!isfinite(y)||x<0||x>1||y<0||y>1) return 0;
    uint16_t hx=(uint16_t)lround(x*32767),hy=(uint16_t)lround(y*32767);
    report[0]=2; report[1]=(uint8_t)hx; report[2]=(uint8_t)(hx>>8);
    report[3]=(uint8_t)hy; report[4]=(uint8_t)(hy>>8); return 1;
}
static inline void irb_hid_mouse(uint8_t buttons,int8_t wheel,uint8_t report[7]) {
    memset(report,0,7); report[0]=3; report[1]=buttons&31; report[6]=(uint8_t)wheel;
}
#endif
