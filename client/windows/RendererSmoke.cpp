#include "BufferedRenderer.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition) { if(!condition) throw std::runtime_error("buffered presentation check failed"); }
int main() {
    HDC target=CreateCompatibleDC(nullptr); HBITMAP bitmap=nullptr; HGDIOBJ original=nullptr;
    try {
        require(target!=nullptr);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=480; info.bmiHeader.biHeight=-640;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        void *pixels=nullptr;
        bitmap=CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels,nullptr,0); require(bitmap!=nullptr);
        original=SelectObject(target,bitmap);
        BufferedRenderer renderer;
        VideoFrame frame; frame.width=30; frame.height=60; frame.bgra.resize(30*60*4);
        RECT image{};
        for(int i=0;i<60;++i) {
            bool red=i%2==0;
            for(size_t p=0;p<frame.bgra.size();p+=4) {
                frame.bgra[p]=red?0:255; frame.bgra[p+1]=0; frame.bgra[p+2]=red?255:0; frame.bgra[p+3]=255;
            }
            // Exercise persistent buffering and a real buffer reallocation.
            int width=i<30?480:420,height=i<30?640:540;
            require(renderer.paint(target,width,height,170,frame,image)); GdiFlush();
            require(GetPixel(target,(image.left+image.right)/2,(image.top+image.bottom)/2)==(red?RGB(255,0,0):RGB(0,0,255)));
            require(GetPixel(target,0,180)==RGB(0,0,0));
            require(GetPixel(target,4,4)==GetSysColor(COLOR_BTNFACE));
            require(image.top>=170&&image.bottom<=height&&image.left>=0&&image.right<=width);
        }
        require(renderer.paint(target,420,540,170,VideoFrame{},image));
        require(image.right==0&&GetPixel(target,210,350)==RGB(0,0,0));
        SelectObject(target,original); DeleteObject(bitmap); DeleteDC(target);
        std::cout<<"Buffered GDI: repeated colored frames, letterboxing, resize and disconnect passed\n";
        return 0;
    } catch(const std::exception &error) {
        if(original) SelectObject(target,original); if(bitmap) DeleteObject(bitmap); if(target) DeleteDC(target);
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
