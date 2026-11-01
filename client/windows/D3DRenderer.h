#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include "VideoFrame.h"

class D3DRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool failed_=false,vsync_=true,fsr_=false,warp_=false;
    int sharpness_=40;
    std::string error_;
public:
    explicit D3DRenderer(bool forceWarp=false);
    ~D3DRenderer();
    void reset();
    void setVSync(bool enabled) { vsync_=enabled; }
    void setFSR(bool enabled) { fsr_=enabled; }
    void setSharpening(int strength);
    bool paint(HWND window,int width,int height,const VideoFrame &frame,RECT &videoRect,
               VideoFrame *diagnosticOutput=nullptr);
    bool fsrActive() const;
    const char *backend() const;
    const std::string &error() const { return error_; }
};
