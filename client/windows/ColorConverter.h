#pragma once
#include "VideoFrame.h"
extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

class ColorConverter {
    SwsContext *context_=nullptr;
    int width_=0,height_=0,format_=-1,range_=-1,matrix_=-1;
public:
    ColorConverter()=default;
    ~ColorConverter();
    ColorConverter(const ColorConverter &)=delete;
    ColorConverter &operator=(const ColorConverter &)=delete;
    void reset();
    void convert(const AVFrame &source,VideoFrame &output);
};
