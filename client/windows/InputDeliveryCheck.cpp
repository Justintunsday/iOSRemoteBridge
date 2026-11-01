#include "CommandWake.h"
#include "QueuedInput.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdlib>
static void check(bool ok) { if(!ok) { std::cerr<<"input delivery check failed\n";std::exit(1); } }
int main() {
    WSADATA data{};check(WSAStartup(MAKEWORD(2,2),&data)==0);
    {
        CommandWake wake;check(wake.valid());
        fd_set readers;FD_ZERO(&readers);FD_SET(wake.reader(),&readers);timeval timeout{2,0};
        auto start=std::chrono::steady_clock::now();
        std::thread producer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(25));wake.signal();});
        int count=select(0,&readers,nullptr,nullptr,&timeout);producer.join();
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        check(count==1&&FD_ISSET(wake.reader(),&readers)&&elapsed<1500);wake.drain();
        FD_ZERO(&readers);FD_SET(wake.reader(),&readers);timeval immediate{};
        check(select(0,&readers,nullptr,nullptr,&immediate)==0);
        std::cout<<"Cross-thread input wake interrupted a two-second network wait in "<<elapsed<<" ms (includes producer delay)\n";
    }
    WSACleanup();
    QueuedInput queue;
    auto move=[](int dx,int buttons=0){return nlohmann::json{{"op","mouse_delta"},{"dx",dx},{"dy",0},{"buttons",buttons},{"wheel",0}};};
    check(queue.push(move(10),true,1)&&queue.push(move(20),true,2));
    check(queue.items.size()==1&&queue.items.front().request["dx"]==30&&queue.items.front().queued==1);
    check(queue.push(move(0,1),false,3)&&queue.push(move(4,1),true,4)&&queue.push(move(0),false,5));
    auto ordered=queue.take();check(ordered.size()==4&&ordered[1].request["buttons"]==1&&ordered[1].request["dx"]==0&&ordered[2].request["dx"]==4&&ordered[3].request["buttons"]==0);
    check(queue.push(move(32767),true,1)&&queue.push(move(1),true,2)&&queue.items.size()==2);queue.clear();
    for(unsigned i=0;i<128;++i) check(queue.push(move(0),false,i));
    check(!queue.push(move(0),false,129));queue.clear();check(queue.take().empty());
    std::cout<<"Motion accumulation, button barriers, bounds and disconnect discard passed\n";
}
