#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "VideoFrame.h"
#include "ColorConverter.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}
class Decoder {
    AVCodecContext *context_=nullptr;
    AVFrame *frame_=nullptr,*cpu_=nullptr;
    AVBufferRef *device_=nullptr;
    ColorConverter converter_;
    bool hardware_=false;
    void open(bool preferHardware);
    void release();
    static AVPixelFormat format(AVCodecContext *,const AVPixelFormat *);
public:
    explicit Decoder(bool preferHardware=true);
    ~Decoder();
    Decoder(const Decoder &)=delete;
    Decoder &operator=(const Decoder &)=delete;
    bool decode(const uint8_t *data,size_t size,VideoFrame &output);
    void fallbackToSoftware();
    bool hardware() const { return hardware_; }
    std::string backend() const { return hardware_?"D3D11VA":"software"; }
};
