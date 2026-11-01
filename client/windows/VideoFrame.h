#pragma once
#include <cstdint>
#include <vector>

struct VideoFrame { int width=0,height=0; std::vector<uint8_t> bgra; };
