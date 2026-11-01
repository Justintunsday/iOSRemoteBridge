#include "Connection.h"
#include "irb_protocol.h"
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <memory>
#include <stdexcept>
using json=nlohmann::json;
using Clock=std::chrono::steady_clock;
static uint64_t nowUS() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count()); }
Connection::Connection(json config,bool control,int fps,bool hardware):config_(std::move(config)),control_(control),preferHardware_(hardware),fps_(fps) {
    for(const char *key:{"token","certificate_sha256"}) {
        auto value=config_.at(key).get<std::string>();
        if(value.size()!=64||!std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isxdigit(c)!=0;})) throw std::runtime_error("invalid pairing configuration");
    }
    config_.at("host").get<std::string>(); int port=config_.value("port",7443);
    if(port<1024||port>65535) throw std::runtime_error("invalid port");
}
Connection::~Connection() { close(); }
void Connection::start() { worker_=std::thread([this]{run();}); }
void Connection::close() {
    stop_=true;
    { std::lock_guard<std::mutex> lock(socketMutex_); if(socket_!=INVALID_SOCKET) shutdown(socket_,SD_BOTH); }
    if(worker_.joinable()) worker_.join();
}
bool Connection::command(json request,bool motionOnly) {
    if(!authorized_) return false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if(!authorized_||stop_) return false;
        if(!commands_.push(std::move(request),motionOnly,nowUS())) return false;
    }
    commandWake_.signal(); return true;
}
void Connection::run() {
    int retries=0;
    while(!stop_) {
        try { session(); }
        catch(const std::exception &e) { if(onStatus) onStatus(e.what()); }
        authorized_=false;
        if(onAuthorization) onAuthorization(false);
        { std::lock_guard<std::mutex> lock(queueMutex_); commands_.clear(); }
        { std::lock_guard<std::mutex> lock(socketMutex_); if(socket_!=INVALID_SOCKET) closesocket(socket_); socket_=INVALID_SOCKET; }
        if(stop_) break;
        // Never replay input when reconnecting; authenticate the paired session again.
        int delay=std::min(30,3*(1+retries++));
        for(int i=0;i<delay*10&&!stop_;++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
void Connection::session() {
    auto host=config_.at("host").get<std::string>(),port=std::to_string(config_.value("port",7443));
    addrinfo hints{}; hints.ai_socktype=SOCK_STREAM; hints.ai_family=AF_INET;
    addrinfo *list=nullptr;
    if(getaddrinfo(host.c_str(),port.c_str(),&hints,&list)) throw std::runtime_error("cannot resolve iPhone address");
    std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> addresses(list,freeaddrinfo);
    SOCKET fd=socket(list->ai_family,list->ai_socktype,list->ai_protocol);
    if(fd==INVALID_SOCKET) throw std::runtime_error("cannot create socket");
    { std::lock_guard<std::mutex> lock(socketMutex_); socket_=fd; }
    DWORD timeout=5000; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char *>(&timeout),sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char *>(&timeout),sizeof(timeout));
    BOOL one=TRUE; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&one),sizeof(one));
    u_long nonblocking=1; ioctlsocket(fd,FIONBIO,&nonblocking);
    if(connect(fd,list->ai_addr,static_cast<int>(list->ai_addrlen))==SOCKET_ERROR) {
        if(WSAGetLastError()!=WSAEWOULDBLOCK) throw std::runtime_error("iPhone TCP connection refused");
        fd_set writeSet,errorSet; FD_ZERO(&writeSet); FD_ZERO(&errorSet); FD_SET(fd,&writeSet); FD_SET(fd,&errorSet);
        timeval wait{5,0};
        if(select(0,nullptr,&writeSet,&errorSet,&wait)<=0||FD_ISSET(fd,&errorSet)) throw std::runtime_error("iPhone TCP connection timed out");
        int error=0,len=sizeof(error); getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char *>(&error),&len);
        if(error) throw std::runtime_error("iPhone TCP connection failed");
    }
    nonblocking=0; ioctlsocket(fd,FIONBIO,&nonblocking);
    std::unique_ptr<SSL_CTX,decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_client_method()),SSL_CTX_free);
    if(!ctx) throw std::runtime_error("TLS context failed");
    SSL_CTX_set_min_proto_version(ctx.get(),TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx.get(),SSL_VERIFY_NONE,nullptr); // Exact certificate pin below is mandatory.
    std::unique_ptr<SSL,decltype(&SSL_free)> tls(SSL_new(ctx.get()),SSL_free);
    if(!tls) throw std::runtime_error("TLS allocation failed");
    SSL_set_fd(tls.get(),static_cast<int>(fd)); SSL_set_tlsext_host_name(tls.get(),host.c_str());
    if(SSL_connect(tls.get())!=1) throw std::runtime_error("TLS handshake failed");
    std::unique_ptr<X509,decltype(&X509_free)> cert(SSL_get1_peer_certificate(tls.get()),X509_free);
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int count=0;
    if(!cert||!X509_digest(cert.get(),EVP_sha256(),digest,&count)||count!=32) throw std::runtime_error("missing server certificate");
    char hex[65]{}; for(unsigned int i=0;i<count;++i) std::snprintf(hex+i*2,3,"%02x",digest[i]);
    auto pin=config_.at("certificate_sha256").get<std::string>();
    std::transform(pin.begin(),pin.end(),pin.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(CRYPTO_memcmp(pin.data(),hex,64)!=0) throw std::runtime_error("CERTIFICATE PIN MISMATCH - no token sent");
    if(X509_cmp_current_time(X509_get0_notBefore(cert.get()))>0||X509_cmp_current_time(X509_get0_notAfter(cert.get()))<0) throw std::runtime_error("paired certificate is not currently valid");
    nonblocking=1; ioctlsocket(fd,FIONBIO,&nonblocking);
    SSL_set_mode(tls.get(),SSL_MODE_ENABLE_PARTIAL_WRITE|SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    std::deque<std::vector<uint8_t>> outgoing;
    uint32_t tx=0,rx=0,request=0; size_t sentOffset=0;
    auto enqueue=[&](uint8_t type,const std::string &payload,uint64_t pts=0) {
        if(payload.size()>IRB_MAX_CONTROL) throw std::runtime_error("request exceeds limit");
        std::vector<uint8_t> packet(IRB_HEADER_SIZE+payload.size());
        irb_header header{type,0,static_cast<uint32_t>(payload.size()),++tx,pts}; irb_pack(packet.data(),header);
        std::copy(payload.begin(),payload.end(),packet.begin()+IRB_HEADER_SIZE); outgoing.push_back(std::move(packet));
        if(outgoing.size()>128) throw std::runtime_error("outgoing queue limit");
    };
    enqueue(IRB_AUTH,json({{"token",config_.at("token")},{"control",control_},{"fps",fps_},{"video_window",3}}).dump());
    if(onStatus) onStatus("Encrypted connection; starting paired session (unlock the iPhone)");
    Decoder decoder(preferHardware_); VideoFrame frame;
    std::vector<uint8_t> received; uint8_t chunk[65536];
    uint64_t lastPing=0,statsAt=nowUS(),lastData=nowUS(),connected=nowUS(); unsigned frames=0;
    double rtt=0,decodeMS=0,inputQueueMS=0; bool waitForKey=false,writeWantsRead=false,readWantsWrite=false;
    while(!stop_) {
        uint64_t now=nowUS();
        if(now-lastData>65000000ULL||(!authorized_&&now-connected>60000000ULL)) throw std::runtime_error("connection/startup timed out");
        if(now-lastPing>1000000) { enqueue(IRB_PING,"",now); lastPing=now; }
        if(authorized_) {
            std::deque<QueuedInput::Item> requests;
            { std::lock_guard<std::mutex> lock(queueMutex_); requests=commands_.take(); }
            for(auto &pending:requests) {
                double waited=(nowUS()-pending.queued)/1000.0;
                inputQueueMS=inputQueueMS*.8+waited*.2;
                pending.request["id"]=++request; enqueue(IRB_CONTROL,pending.request.dump());
            }
        }
        // Flush queued control and frame credits before waiting for more input.
        // A read-only 10-ms wait per packet otherwise delays ACKs behind gestures.
        for(unsigned writes=0;writes<16&&!outgoing.empty();++writes) {
            auto &packet=outgoing.front();
            int result=SSL_write(tls.get(),packet.data()+sentOffset,static_cast<int>(packet.size()-sentOffset));
            if(result>0) { writeWantsRead=false; sentOffset+=static_cast<size_t>(result); if(sentOffset==packet.size()) { outgoing.pop_front(); sentOffset=0; } }
            else { int error=SSL_get_error(tls.get(),result); if(error!=SSL_ERROR_WANT_READ&&error!=SSL_ERROR_WANT_WRITE) throw std::runtime_error("TLS write failed"); writeWantsRead=error==SSL_ERROR_WANT_READ; break; }
        }
        fd_set readSet,writeSet; FD_ZERO(&readSet); FD_ZERO(&writeSet); FD_SET(fd,&readSet);
        if(commandWake_.valid()) FD_SET(commandWake_.reader(),&readSet);
        bool wantsWrite=(!outgoing.empty()&&!writeWantsRead)||readWantsWrite;
        if(wantsWrite) FD_SET(fd,&writeSet);
        timeval delay{0,commandWake_.valid()?10000:1000};
        bool buffered=SSL_pending(tls.get())>0;
        int ready=buffered?1:select(0,&readSet,wantsWrite?&writeSet:nullptr,nullptr,&delay);
        if(ready<0) throw std::runtime_error("socket select failed");
        if(!buffered&&ready>0&&commandWake_.valid()&&FD_ISSET(commandWake_.reader(),&readSet)) {
            commandWake_.drain(); continue; // Flush fresh input before decoding another video packet.
        }
        if(ready>0&&(SSL_pending(tls.get())||FD_ISSET(fd,&readSet)||(readWantsWrite&&FD_ISSET(fd,&writeSet)))) {
            int result=SSL_read(tls.get(),chunk,sizeof(chunk));
            if(result>0) { readWantsWrite=false; received.insert(received.end(),chunk,chunk+result); lastData=nowUS(); }
            else { int error=SSL_get_error(tls.get(),result); if(error!=SSL_ERROR_WANT_READ&&error!=SSL_ERROR_WANT_WRITE) throw std::runtime_error("iPhone disconnected"); readWantsWrite=error==SSL_ERROR_WANT_WRITE; }
        }
        while(received.size()>=IRB_HEADER_SIZE) {
            irb_header header{};
            if(!irb_unpack(received.data(),received.size(),&header)||
               (header.type!=IRB_VIDEO&&header.type!=IRB_RESPONSE&&header.type!=IRB_PONG)) throw std::runtime_error("invalid server frame");
            if(received.size()<IRB_HEADER_SIZE+header.length) break;
            if(header.sequence!=++rx) throw std::runtime_error("server frame sequence mismatch");
            const uint8_t *payload=received.data()+IRB_HEADER_SIZE;
            if(header.type==IRB_PONG) rtt=(nowUS()-header.pts_us)/1000.0;
            else if(header.type==IRB_RESPONSE) {
                auto reply=json::parse(payload,payload+header.length);
                if(reply.value("event",std::string())=="authorized") { authorized_=true; if(onAuthorization) onAuthorization(true); if(onStatus) onStatus("Paired - streaming"); }
                if(onResponse) onResponse(reply);
            } else if(authorized_) {
                auto before=nowUS();
                try {
                    if(!waitForKey||(header.flags&1)) {
                        waitForKey=false;
                        if(decoder.decode(payload,header.length,frame)) { ++frames; if(onFrame) onFrame(std::move(frame)); }
                    }
                } catch(const std::exception &) {
                    if(waitForKey) throw;
                    decoder.fallbackToSoftware(); waitForKey=true;
                    enqueue(IRB_CONTROL,json({{"op","request_keyframe"},{"id",++request}}).dump());
                    if(onStatus) onStatus("Decoder reset to software; requesting keyframe");
                }
                decodeMS=(nowUS()-before)/1000.0;
                enqueue(IRB_CREDIT,"",header.pts_us);
            }
            received.erase(received.begin(),received.begin()+IRB_HEADER_SIZE+header.length);
        }
        if(nowUS()-statsAt>1000000&&authorized_) {
            double seconds=(nowUS()-statsAt)/1000000.;
            char stats[288]; std::snprintf(stats,sizeof(stats),"%.1f FPS | RTT %.1f ms | input queue %.1f ms | decode %.1f ms | %s | F1 rotate, F2 30 FPS, F3 60 FPS, F4 half-size, F5 native",
                    frames/seconds,rtt,inputQueueMS,decodeMS,decoder.backend().c_str());
            if(onStatus) onStatus(stats); statsAt=nowUS(); frames=0;
        }
    }
}
