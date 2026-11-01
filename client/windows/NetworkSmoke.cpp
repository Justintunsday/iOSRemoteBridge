#include "Connection.h"
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
        Connection connection(config,false);
        connection.onStatus=[&](const std::string &s) {
            std::cout<<s<<std::endl;
            if(s.find("PIN MISMATCH")!=std::string::npos) { std::lock_guard<std::mutex> lock(mutex); pinRefused=true; ready.notify_all(); }
        };
        connection.onFrame=[&](VideoFrame &&frame) {
            std::lock_guard<std::mutex> lock(mutex);
            size_t center=(32*64+32)*4;
            pixels=frame.width==64&&frame.height==64&&frame.bgra.size()==64*64*4&&
                   frame.bgra[center]<20&&frame.bgra[center+1]<20&&frame.bgra[center+2]>220;
            ready.notify_all();
        };
        connection.onResponse=[&](const nlohmann::json &r) {
            if(r.value("event",std::string())=="authorized") connection.command({{"op","get_device_info"}});
            if(r.contains("result")&&r["result"].value("model",std::string())=="TLS_SIMULATOR") {
                std::lock_guard<std::mutex> lock(mutex); rpc=r.value("ok",false); ready.notify_all();
            }
        };
        connection.start();
        { std::unique_lock<std::mutex> lock(mutex);
          ready.wait_for(lock,std::chrono::seconds(15),[&]{return wrongPin?pinRefused:(pixels&&rpc);});
          result=(wrongPin?pinRefused:(pixels&&rpc))?0:1;
        }
        connection.close();
        if(!result) std::cout<<(wrongPin?"wrong pin refused before authentication":"TLS -> H.264 -> verified red pixels + RPC passed")<<std::endl;
    } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl; }
    WSACleanup(); return result;
}
