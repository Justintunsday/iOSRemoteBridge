#pragma once
#include <windows.h>
#include "VideoFrame.h"

class BufferedRenderer {
    HDC buffer_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ original_=nullptr;
    int width_=0,height_=0;
    bool resize(HDC target,int width,int height);
public:
    BufferedRenderer()=default;
    ~BufferedRenderer();
    BufferedRenderer(const BufferedRenderer &)=delete;
    BufferedRenderer &operator=(const BufferedRenderer &)=delete;
    void reset();
    bool paint(HDC target,int width,int height,int videoTop,const VideoFrame &frame,RECT &videoRect);
};
