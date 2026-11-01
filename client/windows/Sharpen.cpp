#include "Sharpen.h"
#include <algorithm>
#include <limits>

void SharpenFilter::apply(uint8_t *pixels,size_t stride,int width,int height,int strength) {
    strength=std::clamp(strength,0,100);
    if(!pixels||!strength||width<3||height<3||
       static_cast<size_t>(width)>std::numeric_limits<size_t>::max()/4||
       stride<static_cast<size_t>(width)*4) return;
    previous_.resize(width); current_.resize(width); next_.resize(width);
    auto luma=[&](int y,std::vector<int> &row) {
        const uint8_t *source=pixels+static_cast<size_t>(y)*stride;
        for(int x=0;x<width;++x,source+=4)
            row[x]=(29*source[0]+150*source[1]+77*source[2]+128)>>8;
    };
    luma(0,previous_); luma(1,current_);
    for(int y=1;y<height-1;++y) {
        luma(y+1,next_); // Read the original row before any neighboring writes.
        uint8_t *target=pixels+static_cast<size_t>(y)*stride+4;
        for(int x=1;x<width-1;++x,target+=4) {
            int blur=(previous_[x-1]+2*previous_[x]+previous_[x+1]+
                      2*current_[x-1]+4*current_[x]+2*current_[x+1]+
                      next_[x-1]+2*next_[x]+next_[x+1]+8)>>4;
            int scaled=(current_[x]-blur)*strength;
            int delta=scaled>=0?(scaled+50)/100:-((-scaled+50)/100);
            // Limit halos and retain B-G/R-G differences without clipping a channel.
            int low=std::min({static_cast<int>(target[0]),static_cast<int>(target[1]),static_cast<int>(target[2])});
            int high=std::max({static_cast<int>(target[0]),static_cast<int>(target[1]),static_cast<int>(target[2])});
            delta=std::clamp(delta,std::max(-16,-low),std::min(16,255-high));
            target[0]=static_cast<uint8_t>(target[0]+delta);
            target[1]=static_cast<uint8_t>(target[1]+delta);
            target[2]=static_cast<uint8_t>(target[2]+delta);
        }
        previous_.swap(current_); current_.swap(next_);
    }
}
