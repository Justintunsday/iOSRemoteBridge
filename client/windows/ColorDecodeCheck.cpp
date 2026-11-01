#include "Decoder.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static std::vector<uint8_t> read(const char *path) {
    std::ifstream file(path,std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),{});
    if(data.empty()) throw std::runtime_error("H.264 fixture missing");
    return data;
}
static VideoFrame decode(Decoder &decoder,const std::vector<uint8_t> &data) {
    VideoFrame frame;
    if(!decoder.decode(data.data(),data.size(),frame)) throw std::runtime_error("fixture produced no decoded pixels");
    return frame;
}
int main(int argc,char **argv) {
    if(argc!=3) { std::cerr<<"usage: irb-color-check full.h264 limited.h264 OR --compare single-device-frame.h264\n"; return 2; }
    try {
        if(std::string(argv[1])=="--compare") {
            auto data=read(argv[2]); Decoder software(false),preferred(true);
            VideoFrame a=decode(software,data),b=decode(preferred,data);
            if(a.width!=b.width||a.height!=b.height||a.bgra.size()!=b.bgra.size()) throw std::runtime_error("decoder dimensions differ");
            double total=0; int maximum=0; size_t channels=0;
            for(size_t p=0;p<a.bgra.size();p+=4) for(size_t c=0;c<3;++c) {
                int error=std::abs(static_cast<int>(a.bgra[p+c])-b.bgra[p+c]);
                total+=error; maximum=std::max(maximum,error); ++channels;
            }
            double mean=total/channels;
            std::cout<<"Device frame "<<a.width<<'x'<<a.height<<": software vs "<<preferred.backend()
                     <<" mean RGB error="<<mean<<", max="<<maximum<<" (8-bit levels)\n";
            if(mean>1.5||maximum>6) throw std::runtime_error("hardware/software colors diverged");
        } else {
            const int levels[]={0,8,16,32,64,128,192,224,235,240,248,255};
            for(bool hardware:{false,true}) for(int limited=0;limited<2;++limited) {
                auto data=read(argv[1+limited]); Decoder decoder(hardware); VideoFrame frame=decode(decoder,data);
                if(frame.width!=192||frame.height!=64) throw std::runtime_error("ramp dimensions differ");
                int maximum=0;
                for(int i=0;i<12;++i) {
                    int expected=limited?std::clamp(static_cast<int>(std::lround((levels[i]-16)*255.0/219)),0,255):levels[i];
                    size_t p=(32*192+i*16+8)*4;
                    for(int c=0;c<3;++c) maximum=std::max(maximum,std::abs(static_cast<int>(frame.bgra[p+c])-expected));
                }
                std::cout<<(limited?"limited":"full")<<" H.264 -> "<<decoder.backend()<<": max gray-ramp error="<<maximum<<'\n';
                if(maximum>3) throw std::runtime_error("encoded gray levels clipped or expanded incorrectly");
            }
        }
        return 0;
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
