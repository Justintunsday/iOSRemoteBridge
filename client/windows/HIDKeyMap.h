#pragma once
#include <windows.h>
#include <cstdint>
inline uint16_t hidUsage(WPARAM key,LPARAM flags) {
    if(key>='A'&&key<='Z') return static_cast<uint16_t>(4+key-'A');
    if(key>='1'&&key<='9') return static_cast<uint16_t>(0x1e + key-'1');
    if(key>=VK_F1&&key<=VK_F12) return static_cast<uint16_t>(0x3a+key-VK_F1);
    if(key>=VK_NUMPAD1&&key<=VK_NUMPAD9) return static_cast<uint16_t>(0x59+key-VK_NUMPAD1);
    bool extended=(flags&(1LL<<24))!=0;
    switch(key) {
    case '0':return 0x27; case VK_RETURN:return extended?0x58:0x28;
    case VK_ESCAPE:return 0x29; case VK_BACK:return 0x2a; case VK_TAB:return 0x2b; case VK_SPACE:return 0x2c;
    case VK_OEM_MINUS:return 0x2d; case VK_OEM_PLUS:return 0x2e; case VK_OEM_4:return 0x2f;
    case VK_OEM_6:return 0x30; case VK_OEM_5:return 0x31; case VK_OEM_1:return 0x33;
    case VK_OEM_7:return 0x34; case VK_OEM_3:return 0x35; case VK_OEM_COMMA:return 0x36;
    case VK_OEM_PERIOD:return 0x37; case VK_OEM_2:return 0x38; case VK_CAPITAL:return 0x39;
    case VK_INSERT:return 0x49; case VK_HOME:return 0x4a; case VK_PRIOR:return 0x4b; case VK_DELETE:return 0x4c;
    case VK_END:return 0x4d; case VK_NEXT:return 0x4e; case VK_RIGHT:return 0x4f; case VK_LEFT:return 0x50;
    case VK_DOWN:return 0x51; case VK_UP:return 0x52; case VK_NUMLOCK:return 0x53; case VK_DIVIDE:return 0x54;
    case VK_MULTIPLY:return 0x55; case VK_SUBTRACT:return 0x56; case VK_ADD:return 0x57;
    case VK_NUMPAD0:return 0x62; case VK_DECIMAL:return 0x63;
    case VK_SHIFT:return ((flags>>16)&0xff)==0x36?0xe5:0xe1;
    case VK_CONTROL:return extended?0xe4:0xe0; case VK_MENU:return extended?0xe6:0xe2;
    case VK_LSHIFT:return 0xe1; case VK_RSHIFT:return 0xe5; case VK_LCONTROL:return 0xe0; case VK_RCONTROL:return 0xe4;
    case VK_LMENU:return 0xe2; case VK_RMENU:return 0xe6;
    default:return 0;
    }
}
