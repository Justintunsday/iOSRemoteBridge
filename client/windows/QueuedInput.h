#pragma once
#include <nlohmann/json.hpp>
#include <deque>
#include <cstdint>
struct QueuedInput {
    struct Item { nlohmann::json request; bool motion; uint64_t queued; };
    std::deque<Item> items;
    bool push(nlohmann::json next,bool motion,uint64_t queued) {
        motion=motion&&next.value("op",std::string())=="mouse_delta"&&next.value("wheel",0)==0;
        if(!items.empty()) {
            auto &last=items.back(); auto &previous=last.request;
            if(next.value("op",std::string())=="touch"&&next.value("phase",-1)==1&&
               previous.value("op",std::string())=="touch"&&previous.value("phase",-1)==1&&
               previous.value("finger",-1)==next.value("finger",-2)) { previous=std::move(next); return true; }
            if(motion&&last.motion&&previous.value("buttons",-1)==next.value("buttons",-2)) {
                int64_t dx=previous.value("dx",int64_t{})+next.value("dx",int64_t{});
                int64_t dy=previous.value("dy",int64_t{})+next.value("dy",int64_t{});
                if(dx>=-32767&&dx<=32767&&dy>=-32767&&dy<=32767) { previous["dx"]=dx; previous["dy"]=dy; return true; }
            }
        }
        if(items.size()>=128) return false;
        items.push_back({std::move(next),motion,queued}); return true;
    }
    void clear() { items.clear(); }
    std::deque<Item> take() { std::deque<Item> ready; ready.swap(items); return ready; }
};
