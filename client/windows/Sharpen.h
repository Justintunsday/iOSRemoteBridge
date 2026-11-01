#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Reuses three original luma rows while updating a BGRA image in place.
class SharpenFilter {
    std::vector<int> previous_,current_,next_;
public:
    void apply(uint8_t *pixels,size_t stride,int width,int height,int strength);
};
