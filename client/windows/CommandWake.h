#pragma once
#include <winsock2.h>
/* A loopback-only byte wakes select when the UI queues input. No credentials
   or commands travel over these sockets; the existing pinned TLS carries them. */
class CommandWake {
    SOCKET receive_=INVALID_SOCKET,send_=INVALID_SOCKET;
public:
    CommandWake() {
        receive_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); send_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(receive_==INVALID_SOCKET||send_==INVALID_SOCKET) { reset(); return; }
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        int length=sizeof(address);
        if(bind(receive_,reinterpret_cast<sockaddr *>(&address),sizeof(address))||
           getsockname(receive_,reinterpret_cast<sockaddr *>(&address),&length)||
           connect(send_,reinterpret_cast<sockaddr *>(&address),sizeof(address))) { reset(); return; }
        u_long nonblocking=1;
        if(ioctlsocket(receive_,FIONBIO,&nonblocking)||ioctlsocket(send_,FIONBIO,&nonblocking)) reset();
    }
    ~CommandWake() { reset(); }
    CommandWake(const CommandWake &)=delete;
    CommandWake &operator=(const CommandWake &)=delete;
    void reset() {
        if(receive_!=INVALID_SOCKET) closesocket(receive_);
        if(send_!=INVALID_SOCKET) closesocket(send_);
        receive_=send_=INVALID_SOCKET;
    }
    SOCKET reader() const { return receive_; }
    bool valid() const { return receive_!=INVALID_SOCKET&&send_!=INVALID_SOCKET; }
    void signal() { if(valid()) send(send_,"!",1,0); }
    void drain() { char bytes[64]; for(unsigned i=0;i<256&&recv(receive_,bytes,sizeof(bytes),0)>0;++i) {} }
};
