#include "BufferedRenderer.h"
#include <algorithm>
#include <cmath>

BufferedRenderer::~BufferedRenderer() { reset(); }
void BufferedRenderer::reset() {
    if(buffer_&&original_) SelectObject(buffer_,original_);
    if(bitmap_) DeleteObject(bitmap_);
    if(buffer_) DeleteDC(buffer_);
    buffer_=nullptr; bitmap_=nullptr; original_=nullptr; pixels_=nullptr; width_=height_=0;
}
void BufferedRenderer::setSharpening(int strength) { sharpening_=std::clamp(strength,0,100); }
bool BufferedRenderer::resize(HDC target,int width,int height) {
    if(width<=0||height<=0) return false;
    if(buffer_&&width_==width&&height_==height) return true;
    HDC next=CreateCompatibleDC(target);
    if(!next) return false;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void *pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap) { DeleteDC(next); return false; }
    HGDIOBJ original=SelectObject(next,bitmap);
    if(!original||original==HGDI_ERROR) { DeleteObject(bitmap); DeleteDC(next); return false; }
    reset(); buffer_=next; bitmap_=bitmap; original_=original; width_=width; height_=height;
    pixels_=static_cast<uint8_t *>(pixels);
    return true;
}
bool BufferedRenderer::paint(HDC target,int width,int height,int videoTop,const VideoFrame &frame,RECT &videoRect) {
    videoRect={};
    if(!resize(target,width,height)) return false;
    RECT client{0,0,width,height},area{0,std::min(videoTop,height),width,height};
    // All clearing and scaling happen offscreen. Present only the completed
    // image, so a paint never exposes a black/gray erase between video frames.
    FillRect(buffer_,&client,GetSysColorBrush(COLOR_BTNFACE));
    FillRect(buffer_,&area,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if(frame.width>0&&frame.height>0&&area.bottom>area.top&&
       frame.bgra.size()>=static_cast<size_t>(frame.width)*frame.height*4) {
        double scale=std::min(static_cast<double>(width)/frame.width,
                              static_cast<double>(area.bottom-area.top)/frame.height);
        int shownWidth=std::max(1,static_cast<int>(std::lround(frame.width*scale)));
        int shownHeight=std::max(1,static_cast<int>(std::lround(frame.height*scale)));
        int left=(width-shownWidth)/2,top=area.top+(area.bottom-area.top-shownHeight)/2;
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=frame.width; info.bmiHeader.biHeight=-frame.height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        SetStretchBltMode(buffer_,COLORONCOLOR);
        int lines=StretchDIBits(buffer_,left,top,shownWidth,shownHeight,0,0,frame.width,frame.height,
                               frame.bgra.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        if(!lines||lines==GDI_ERROR) return false;
        videoRect={left,top,left+shownWidth,top+shownHeight};
        if(sharpening_) {
            // Complete GDI writes before accessing DIB memory. Sharpen only the
            // scaled video, then present once, preserving flicker-free painting.
            GdiFlush();
            sharpen_.apply(pixels_+(static_cast<size_t>(top)*width_+left)*4,
                           static_cast<size_t>(width_)*4,shownWidth,shownHeight,sharpening_);
        }
    }
    return BitBlt(target,0,0,width,height,buffer_,0,0,SRCCOPY)!=FALSE;
}
