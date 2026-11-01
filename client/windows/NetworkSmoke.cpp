#include "Connection.h"
#include "RpcReply.h"
#include <condition_variable>
#include <fstream>
#include <iostream>
int main(int argc,char **argv) {
    if(argc<2) return 2;
    WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) return 2;
    int result=1;
    try {
        std::ifstream input(argv[1]); nlohmann::json config; input>>config;
        bool wrongPin=argc>2&&std::string(argv[2])=="--expect-pin-failure";
        bool pixels=false,rpc=false,pinRefused=false;
        std::mutex mutex; std::condition_variable ready;
        unsigned frameCount=0,responseCount=0;
        const std::vector<nlohmann::json> actions={
            {{"op","get_device_info"}},{{"op","press_home"}},
            {{"op","touch"},{"finger",0},{"phase",0},{"x",.4},{"y",.5}},
            {{"op","touch"},{"finger",0},{"phase",1},{"x",.5},{"y",.5}},
            {{"op","touch"},{"finger",0},{"phase",2},{"x",.6},{"y",.5}},
            {{"op","tap"},{"x",-1},{"y",.5}},{{"op","get_device_info"}}
        };
        Connection connection(config,true);
        connection.onStatus=[&](const std::string &s) {
            std::cout<<s<<std::endl;
            if(s.find("PIN MISMATCH")!=std::string::npos) { std::lock_guard<std::mutex> lock(mutex); pinRefused=true; ready.notify_all(); }
        };
        connection.onFrame=[&](VideoFrame &&frame) {
            std::lock_guard<std::mutex> lock(mutex);
            size_t center=(32*64+32)*4;
            bool red=frame.width==64&&frame.height==64&&frame.bgra.size()==64*64*4&&
                     frame.bgra[center]<20&&frame.bgra[center+1]<20&&frame.bgra[center+2]>220;
            pixels=red&&++frameCount>=3;
            ready.notify_all();
        };
        connection.onResponse=[&](const nlohmann::json &r) {
            if(r.value("event",std::string())=="authorized") { connection.command(actions[0]); return; }
            if(!r.contains("ok")) return;
            bool ok=responseSucceeded(r);
            if(responseCount>=actions.size()||r.at("id")!=responseCount+1||ok!=(responseCount!=5))
                throw std::runtime_error("action response did not match the pending request");
            ++responseCount;
            if(responseCount<actions.size()) connection.command(actions[responseCount]);
            else { std::lock_guard<std::mutex> lock(mutex); rpc=true; ready.notify_all(); }
        };
        connection.start();
        { std::unique_lock<std::mutex> lock(mutex);
          ready.wait_for(lock,std::chrono::seconds(15),[&]{return wrongPin?pinRefused:(pixels&&rpc);});
          result=(wrongPin?pinRefused:(pixels&&rpc))?0:1;
        }
        connection.close();
        if(!result) std::cout<<(wrongPin?"wrong pin refused before authentication":"Two-frame TLS/H.264 pipeline: ordered credits, repeated actions, legacy 0/1 replies, rejection and post-input pixels passed")<<std::endl;
    } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl; }
    WSACleanup(); return result;
}
