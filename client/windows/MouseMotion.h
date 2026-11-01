#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
struct MouseMotion {
    int64_t x=0,y=0;
    void clear() { x=y=0; }
    bool pending() const { return x||y; }
    void add(int dx,int dy,int rotation) {
        int64_t a=dx,b=dy;
        if(rotation==90) { a=dy; b=-static_cast<int64_t>(dx); }
        else if(rotation==180) { a=-a; b=-b; }
        else if(rotation==270) { a=-static_cast<int64_t>(dy); b=dx; }
        constexpr int64_t limit=1LL<<30;
        x=std::clamp(x+a,-limit,limit); y=std::clamp(y+b,-limit,limit);
    }
    std::pair<int,int> take() {
        int dx=static_cast<int>(std::clamp<int64_t>(x,-32767,32767));
        int dy=static_cast<int>(std::clamp<int64_t>(y,-32767,32767));
        x-=dx; y-=dy; return {dx,dy};
    }
};
