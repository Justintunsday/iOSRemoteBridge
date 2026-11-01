#include "ColorConverter.h"
#include <stdexcept>
extern "C" {
#include <libavutil/pixdesc.h>
}

ColorConverter::~ColorConverter() { reset(); }
void ColorConverter::reset() {
    sws_freeContext(context_); context_=nullptr;
    width_=height_=0; format_=range_=matrix_=-1;
}
void ColorConverter::convert(const AVFrame &source,VideoFrame &output) {
    if(source.width<=0||source.height<=0||source.width>4096||source.height>4096)
        throw std::runtime_error("unsafe decoded dimensions");
    AVPixelFormat format=static_cast<AVPixelFormat>(source.format);
    const AVPixFmtDescriptor *description=av_pix_fmt_desc_get(format);
    if(!description) throw std::runtime_error("unknown decoded pixel format");
    int full=source.color_range==AVCOL_RANGE_JPEG;
    if(source.color_range==AVCOL_RANGE_UNSPECIFIED) {
        full=(description->flags&AV_PIX_FMT_FLAG_RGB)||format==AV_PIX_FMT_YUVJ420P||
             format==AV_PIX_FMT_YUVJ422P||format==AV_PIX_FMT_YUVJ444P||format==AV_PIX_FMT_YUVJ440P;
    }
    int matrix=SWS_CS_DEFAULT;
    switch(source.colorspace) {
    case AVCOL_SPC_BT709: matrix=SWS_CS_ITU709; break;
    case AVCOL_SPC_FCC: matrix=SWS_CS_FCC; break;
    case AVCOL_SPC_BT470BG: case AVCOL_SPC_SMPTE170M: matrix=SWS_CS_ITU601; break;
    case AVCOL_SPC_SMPTE240M: matrix=SWS_CS_SMPTE240M; break;
    case AVCOL_SPC_BT2020_NCL: matrix=SWS_CS_BT2020; break;
    default: break; // Preserve FFmpeg's BT.601 default for untagged legacy video.
    }
    bool changed=width_!=source.width||height_!=source.height||format_!=source.format;
    context_=sws_getCachedContext(context_,source.width,source.height,format,
              source.width,source.height,AV_PIX_FMT_BGRA,SWS_BILINEAR|SWS_ACCURATE_RND|SWS_FULL_CHR_H_INT,nullptr,nullptr,nullptr);
    if(!context_) throw std::runtime_error("pixel conversion unavailable");
    if(changed||range_!=full||matrix_!=matrix) {
        const int *coefficients=sws_getCoefficients(matrix);
        // Source range comes from the decoded frame, including after GPU
        // download. GDI BGRA is full-range; brightness/contrast remain neutral.
        if(sws_setColorspaceDetails(context_,coefficients,full,coefficients,1,0,1<<16,1<<16)<0)
            throw std::runtime_error("video color range/matrix configuration failed");
        width_=source.width; height_=source.height; format_=source.format; range_=full; matrix_=matrix;
    }
    output.width=source.width; output.height=source.height;
    output.bgra.resize(static_cast<size_t>(output.width)*output.height*4);
    uint8_t *planes[]={output.bgra.data(),nullptr,nullptr,nullptr};
    int strides[]={output.width*4,0,0,0};
    if(sws_scale(context_,source.data,source.linesize,0,source.height,planes,strides)!=source.height)
        throw std::runtime_error("incomplete BGRA conversion");
}
