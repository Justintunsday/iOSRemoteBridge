#pragma once
#include <memory>
#include <string>
#include <vector>
#include "VideoFrame.h"

// Video has no engine depth/motion/jitter. This opt-in experiment resets history
// every frame and supplies a flat plane, zero motion and fixed exposure.
class ExperimentalFSR {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_,provider_;
    bool failed_=false;
public:
    ExperimentalFSR();
    ~ExperimentalFSR();
    void reset();
    bool probe(std::vector<std::string> &versions);
    bool process(const VideoFrame &input,int width,int height,VideoFrame &output);
    const std::string &provider() const { return provider_; }
    const std::string &error() const { return error_; }
};
