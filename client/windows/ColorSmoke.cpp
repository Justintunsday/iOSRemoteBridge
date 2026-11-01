#include "ColorConverter.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool ok) { if(!ok) throw std::runtime_error("color range/matrix regression"); }
int main() {
    AVFrame *source=av_frame_alloc(),*download=av_frame_alloc();
    try {
        require(source&&download);
        source->format=AV_PIX_FMT_NV12; source->width=192; source->height=32;
        require(av_frame_get_buffer(source,32)>=0);
        const int levels[]={0,8,16,32,64,128,192,224,235,240,248,255};
        ColorConverter converter; VideoFrame pixels;
        for(AVColorRange range:{AVCOL_RANGE_JPEG,AVCOL_RANGE_MPEG,AVCOL_RANGE_JPEG}) {
            source->color_range=range; source->colorspace=AVCOL_SPC_BT709;
            for(int y=0;y<source->height;++y) for(int x=0;x<source->width;++x)
                source->data[0][y*source->linesize[0]+x]=static_cast<uint8_t>(levels[x/16]);
            for(int y=0;y<source->height/2;++y) std::memset(source->data[1]+y*source->linesize[1],128,source->width);
            // GPU download produces NV12 while color properties live on the
            // original frame. Verify the same copy+conversion used by Decoder.
            av_frame_unref(download); download->format=source->format;
            download->width=source->width; download->height=source->height;
            require(av_frame_get_buffer(download,32)>=0&&av_frame_copy(download,source)>=0&&av_frame_copy_props(download,source)>=0);
            converter.convert(*download,pixels);
            for(int i=0;i<12;++i) {
                int expected=range==AVCOL_RANGE_JPEG?levels[i]:std::clamp(static_cast<int>(std::lround((levels[i]-16)*255.0/219)),0,255);
                size_t p=(16*192+i*16+8)*4;
                for(int c=0;c<3;++c) require(std::abs(static_cast<int>(pixels.bgra[p+c])-expected)<=2);
            }
        }
        // Identical YUV bytes require different matrices; the context must
        // update without a resolution/pixel-format change.
        for(AVColorSpace matrix:{AVCOL_SPC_BT709,AVCOL_SPC_SMPTE170M,AVCOL_SPC_BT709}) {
            source->colorspace=matrix; source->color_range=AVCOL_RANGE_JPEG;
            int y=matrix==AVCOL_SPC_BT709?54:76,u=matrix==AVCOL_SPC_BT709?99:85;
            for(int row=0;row<32;++row) std::memset(source->data[0]+row*source->linesize[0],y,192);
            for(int row=0;row<16;++row) for(int x=0;x<192;x+=2) {
                source->data[1][row*source->linesize[1]+x]=static_cast<uint8_t>(u);
                source->data[1][row*source->linesize[1]+x+1]=255;
            }
            converter.convert(*source,pixels); size_t p=(16*192+96)*4;
            require(pixels.bgra[p]<4&&pixels.bgra[p+1]<4&&pixels.bgra[p+2]>251);
        }
        av_frame_free(&source); av_frame_free(&download);
        std::cout<<"Full/limited NV12 ramps preserve black, near-white detail and BT.601/709 colors across metadata changes\n";
        return 0;
    } catch(const std::exception &error) {
        av_frame_free(&source); av_frame_free(&download); std::cerr<<error.what()<<'\n'; return 1;
    }
}
