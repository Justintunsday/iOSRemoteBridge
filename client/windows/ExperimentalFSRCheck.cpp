#include "ExperimentalFSR.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc,char **argv) {
    ExperimentalFSR fsr; std::vector<std::string> versions;
    if(!fsr.probe(versions)) { std::cerr<<fsr.error()<<'\n'; return 1; }
    std::cout<<"Available SDK providers:"; for(auto &version:versions) std::cout<<' '<<version; std::cout<<'\n';
    if(argc>1&&std::string(argv[1])=="--probe") return 0;
    bool phone=argc>1&&std::string(argv[1])=="--phone";
    VideoFrame input; input.width=phone?375:128; input.height=phone?667:128;
    int width=input.width*2,height=input.height*2;
    input.bgra.resize(static_cast<size_t>(input.width)*input.height*4);
    for(size_t i=0;i<input.bgra.size();i+=4) { input.bgra[i]=83; input.bgra[i+1]=125; input.bgra[i+2]=187; input.bgra[i+3]=255; }
    VideoFrame output; auto start=std::chrono::steady_clock::now();
    if(!fsr.process(input,width,height,output)) { std::cerr<<fsr.error()<<'\n'; return 1; }
    size_t p=(static_cast<size_t>(height/2)*width+width/2)*4; int maximum=0;
    for(int channel=0;channel<3;++channel) maximum=std::max(maximum,std::abs(output.bgra[p+channel]-input.bgra[channel]));
    std::cout<<"Selected SDK provider: "<<fsr.provider()<<"; single-frame reset=true; flat-color max error="<<maximum<<'\n';
    if(maximum>4) return 1;
    start=std::chrono::steady_clock::now();
    for(int i=0;i<12;++i) if(!fsr.process(input,width,height,output)) { std::cerr<<fsr.error()<<'\n'; return 1; }
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/12;
    std::cout<<input.width<<'x'<<input.height<<" -> "<<width<<'x'<<height<<" experiment: mean CPU conversion/upload/GPU/readback="<<ms<<" ms (not display latency)\n";
    return 0;
}
