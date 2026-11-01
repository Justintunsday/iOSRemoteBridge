#include "Decoder.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char **argv) {
    if(argc!=2) { std::cerr<<"usage: decoder-smoke fixture.h264\n"; return 2; }
    try {
        std::string license=avcodec_license();
        std::cout<<"FFmpeg license: "<<license<<"\nFFmpeg configuration: "<<avcodec_configuration()<<"\n";
        if(license.find("LGPL version 2.1 or later")==std::string::npos) throw std::runtime_error("unexpected FFmpeg license configuration");
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        if(bytes.empty()) throw std::runtime_error("fixture missing");
        Decoder decoder(false); VideoFrame frame;
        if(!decoder.decode(bytes.data(),bytes.size(),frame)||frame.width!=64||frame.height!=64||frame.bgra.size()!=64*64*4) throw std::runtime_error("decoded frame mismatch");
        // The fixture is a solid red frame. Verify actual converted pixels.
        auto center=(32*64+32)*4;
        if(frame.bgra[center]>20||frame.bgra[center+1]>20||frame.bgra[center+2]<220) throw std::runtime_error("decoded pixels differ");
        std::cout<<"64x64 H.264 -> BGRA verified ("<<decoder.backend()<<")\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
