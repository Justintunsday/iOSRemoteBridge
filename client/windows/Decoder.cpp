#include "Decoder.h"
#include <stdexcept>
#include <cstring>
#include <limits>
static std::string avError(int code) {
    char message[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code,message,sizeof(message)); return message;
}
AVPixelFormat Decoder::format(AVCodecContext *context,const AVPixelFormat *formats) {
    auto *self=static_cast<Decoder *>(context->opaque);
    for(auto *p=formats;*p!=AV_PIX_FMT_NONE;++p) if(*p==AV_PIX_FMT_D3D11) { self->hardware_=true; return *p; }
    self->hardware_=false;
    for(auto *p=formats;*p!=AV_PIX_FMT_NONE;++p) if(*p==AV_PIX_FMT_YUV420P||*p==AV_PIX_FMT_NV12) return *p;
    return formats[0];
}
Decoder::Decoder(bool preferHardware) { open(preferHardware); }
void Decoder::open(bool preferHardware) {
    const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_H264);
    if(!codec) throw std::runtime_error("H.264 decoder unavailable");
    context_=avcodec_alloc_context3(codec); frame_=av_frame_alloc(); cpu_=av_frame_alloc();
    if(!context_||!frame_||!cpu_) { release(); throw std::runtime_error("decoder allocation failed"); }
    context_->flags|=AV_CODEC_FLAG_LOW_DELAY; context_->thread_count=1; context_->opaque=this;
    if(preferHardware) {
        bool supported=false;
        for(int i=0;;++i) {
            const AVCodecHWConfig *config=avcodec_get_hw_config(codec,i); if(!config) break;
            if(config->device_type==AV_HWDEVICE_TYPE_D3D11VA&&(config->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) supported=true;
        }
        if(supported&&av_hwdevice_ctx_create(&device_,AV_HWDEVICE_TYPE_D3D11VA,nullptr,nullptr,0)>=0) {
            context_->hw_device_ctx=av_buffer_ref(device_); context_->get_format=format;
        }
    }
    int error=avcodec_open2(context_,codec,nullptr);
    if(error<0) { release(); if(preferHardware) { open(false); return; } throw std::runtime_error("avcodec_open2: "+avError(error)); }
}
void Decoder::release() {
    avcodec_free_context(&context_); av_frame_free(&frame_); av_frame_free(&cpu_);
    av_buffer_unref(&device_); converter_.reset(); hardware_=false;
}
Decoder::~Decoder() { release(); }
void Decoder::fallbackToSoftware() { release(); open(false); }
bool Decoder::decode(const uint8_t *data,size_t size,VideoFrame &output) {
    if(size>static_cast<size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("packet too large");
    AVPacket *packet=av_packet_alloc();
    if(!packet) throw std::runtime_error("packet allocation failed");
    int error=av_new_packet(packet,static_cast<int>(size));
    if(error>=0) { std::memcpy(packet->data,data,size); error=avcodec_send_packet(context_,packet); }
    av_packet_free(&packet);
    if(error<0) throw std::runtime_error("decode input: "+avError(error));
    bool produced=false;
    while((error=avcodec_receive_frame(context_,frame_))>=0) {
        AVFrame *source=frame_;
        if(frame_->format==AV_PIX_FMT_D3D11) {
            av_frame_unref(cpu_);
            int transfer=av_hwframe_transfer_data(cpu_,frame_,0);
            if(transfer<0) { av_frame_unref(frame_); throw std::runtime_error("GPU frame transfer: "+avError(transfer)); }
            // av_hwframe_transfer_data copies pixel storage, not AVFrame color
            // properties. Losing JPEG/full range here over-expands highlights.
            int properties=av_frame_copy_props(cpu_,frame_);
            if(properties<0) { av_frame_unref(frame_); throw std::runtime_error("GPU frame properties: "+avError(properties)); }
            source=cpu_;
        }
        converter_.convert(*source,output);
        av_frame_unref(frame_); produced=true;
    }
    if(error!=AVERROR(EAGAIN)&&error!=AVERROR_EOF) throw std::runtime_error("decode output: "+avError(error));
    return produced;
}
