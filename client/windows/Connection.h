#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include "Decoder.h"
class Connection {
    using json=nlohmann::json;
    std::atomic<bool> stop_{false},authorized_{false};
    std::thread worker_;
    std::mutex queueMutex_,socketMutex_;
    std::deque<json> commands_;
    SOCKET socket_=INVALID_SOCKET;
    json config_;
    bool control_,preferHardware_;
    int fps_;
    void run();
    void session();
public:
    std::function<void(VideoFrame &&)> onFrame;
    std::function<void(const std::string &)> onStatus;
    std::function<void(const json &)> onResponse;
    Connection(json config,bool control=true,int fps=30,bool hardware=true);
    ~Connection();
    void start();
    void close();
    bool authorized() const { return authorized_; }
    bool command(json request);
};
