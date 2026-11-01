#include "../client/windows/Sharpen.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
static std::vector<uint8_t> grayRows(const std::vector<uint8_t> &row,int height) {
    std::vector<uint8_t> pixels(row.size()*height*4);
    for(size_t p=0;p<pixels.size();p+=4) {
        pixels[p]=pixels[p+1]=pixels[p+2]=row[(p/4)%row.size()]; pixels[p+3]=173;
    }
    return pixels;
}
int main() {
    try {
        SharpenFilter filter;
        auto flat=grayRows({72,72,72,72,72,72,72,72,72},7);
        for(size_t p=0;p<flat.size();p+=4) { flat[p]=37; flat[p+1]=111; flat[p+2]=203; }
        auto original=flat;
        filter.apply(flat.data(),9*4,9,7,100);
        require(flat==original,"flat colors or alpha changed");

        auto edge=grayRows({40,40,40,80,175,215,215,215,215},7);
        original=edge; filter.apply(edge.data(),9*4,9,7,0);
        require(edge==original,"zero strength changed pixels");
        filter.apply(edge.data(),9*4,9,7,40);
        size_t left=(3*9+3)*4,right=left+4;
        require(edge[right]-edge[left]>original[right]-original[left],"text edge contrast did not increase");
        auto stronger=original; filter.apply(stronger.data(),9*4,9,7,80);
        require(stronger[right]-stronger[left]>=edge[right]-edge[left],"higher strength reduced edge contrast");

        auto ramp=grayRows({20,40,60,80,100,120,140,160,180},5);
        original=ramp; filter.apply(ramp.data(),9*4,9,5,100);
        require(ramp==original,"smooth linear tone ramp changed");

        // A video rectangle can be an offset into a larger DIB with row padding.
        constexpr int width=9,height=7; constexpr size_t offset=11,stride=48;
        std::vector<uint8_t> padded(offset+stride*height+13,0xa5);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            size_t p=offset+y*stride+x*4;
            int level=x<4?50:150;
            padded[p]=static_cast<uint8_t>(level); padded[p+1]=static_cast<uint8_t>(level+30);
            padded[p+2]=static_cast<uint8_t>(level+60); padded[p+3]=static_cast<uint8_t>(y*width+x);
        }
        original=padded; filter.apply(padded.data()+offset,stride,width,height,100);
        for(size_t i=0;i<padded.size();++i) {
            size_t relative=i>=offset?i-offset:stride*height;
            size_t y=relative/stride,x=(relative%stride)/4,channel=(relative%stride)%4;
            bool interior=y>0&&y<height-1&&x>0&&x<width-1&&channel<3;
            if(!interior) require(padded[i]==original[i],"filter wrote outside video interior or changed alpha");
        }
        for(int y=1;y<height-1;++y) for(int x=1;x<width-1;++x) {
            size_t p=offset+y*stride+x*4;
            require(padded[p+1]-padded[p]==30&&padded[p+2]-padded[p+1]==30,"chroma differences changed");
            require(std::abs(static_cast<int>(padded[p])-original[p])<=16,"edge halo exceeded bound");
        }
        auto tiny=grayRows({0,255},2); original=tiny;
        filter.apply(tiny.data(),8,2,2,100); require(tiny==original,"tiny image changed");
        filter.apply(nullptr,0,9,9,100);
        std::cout<<"Sharpening: edge contrast, neutral tones, chroma, alpha and rectangle bounds passed\n";
        return 0;
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
