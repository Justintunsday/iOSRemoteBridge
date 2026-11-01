#import <Foundation/Foundation.h>
#import <Network/Network.h>
#import <Security/Security.h>
#import <Security/SecProtocolOptions.h>
#import <CommonCrypto/CommonDigest.h>
#import <sys/socket.h>
#import <sys/un.h>
#import <sys/stat.h>
#import <fcntl.h>
#import <unistd.h>
#import <signal.h>
#import "../common/IRBLocalPeer.h"

static NSData *privateFile(NSString *path,NSUInteger max) {
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);
    if(fd<0) return nil;
    struct stat st; BOOL ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&
        (st.st_uid==getuid()||st.st_uid==0)&&!(st.st_mode&0077)&&st.st_size>0&&(uint64_t)st.st_size<=max;
    NSMutableData *data=ok?[NSMutableData dataWithLength:(NSUInteger)st.st_size]:nil;
    size_t done=0;
    while(ok&&done<data.length) { ssize_t n=read(fd,(uint8_t *)data.mutableBytes+done,data.length-done); if(n<=0) { ok=NO; break; } done+=(size_t)n; }
    close(fd); return ok?data:nil;
}
static BOOL tokenEqual(NSString *a,NSString *b) {
    if(![a isKindOfClass:NSString.class]||![b isKindOfClass:NSString.class]) return NO;
    NSData *aa=[a dataUsingEncoding:NSUTF8StringEncoding],*bb=[b dataUsingEncoding:NSUTF8StringEncoding];
    uint8_t ah[CC_SHA256_DIGEST_LENGTH],bh[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(aa.bytes,(CC_LONG)aa.length,ah); CC_SHA256(bb.bytes,(CC_LONG)bb.length,bh);
    volatile unsigned diff=0; for(int i=0;i<CC_SHA256_DIGEST_LENGTH;++i) diff|=ah[i]^bh[i];
    return diff==0;
}
@interface IRBServer : NSObject
- (BOOL)start;
@end
@implementation IRBServer {
    NSDictionary *_config;
    nw_listener_t _listener;
    nw_connection_t _connection;
    IRBLocalPeer *_bridge;
    NSMutableData *_received;
    uint32_t _rx,_tx;
    uint64_t _generation,_cooldown,_connectedAt,_lastActivity,_ackPTS,_videoSentAt,_rateStart;
    NSUInteger _pendingBytes,_commands;
    BOOL _authenticated,_authorized,_awaitingAck;
    int _localSocket;
}
- (BOOL)start {
    _config=IRBDictionary(privateFile(IRB_DIRECTORY @"/device-config.json",IRB_MAX_CONTROL));
    NSString *token=_config[@"token"],*pass=_config[@"p12_password"];
    NSNumber *port=_config[@"port"];
    if(![token isKindOfClass:NSString.class]||token.length!=64||![pass isKindOfClass:NSString.class]||pass.length<32||
       ![port isKindOfClass:NSNumber.class]||port.intValue<1024||port.intValue>65535) {
        NSLog(@"[IRB] Missing/invalid private configuration (must be owner-only mode 0600)."); return NO;
    }
    NSData *p12=privateFile(IRB_DIRECTORY @"/server.p12",65536); CFArrayRef items=NULL;
    OSStatus status=p12?SecPKCS12Import((__bridge CFDataRef)p12,
           (__bridge CFDictionaryRef)@{(id)kSecImportExportPassphrase:pass},&items):errSecParam;
    if(status||!items||CFArrayGetCount(items)==0) { if(items) CFRelease(items); NSLog(@"[IRB] PKCS12 identity could not be loaded (%d).",(int)status); return NO; }
    NSDictionary *first=(__bridge NSDictionary *)CFArrayGetValueAtIndex(items,0);
    SecIdentityRef identity=(__bridge SecIdentityRef)first[(id)kSecImportItemIdentity];
    sec_identity_t localIdentity=identity?sec_identity_create(identity):nil;
    CFRelease(items);
    if(!localIdentity) return NO;
    nw_parameters_t params=nw_parameters_create_secure_tcp(^(nw_protocol_options_t tls) {
        sec_protocol_options_t options=nw_tls_copy_sec_protocol_options(tls);
        sec_protocol_options_set_min_tls_protocol_version(options,tls_protocol_version_TLSv12);
        sec_protocol_options_set_local_identity(options,localIdentity);
    },^(nw_protocol_options_t tcp) { nw_tcp_options_set_no_delay(tcp,true); });
    // This prototype listens on Wi-Fi. No UPnP/NAT traversal or public exposure setup.
    nw_parameters_set_required_interface_type(params,nw_interface_type_wifi);
    NSString *portText=port.stringValue;
    nw_parameters_set_local_endpoint(params,nw_endpoint_create_host("0.0.0.0",portText.UTF8String));
    _listener=nw_listener_create(params);
    if(!_listener) return NO;
    nw_listener_set_queue(_listener,dispatch_get_main_queue());
    __weak IRBServer *weak=self;
    nw_listener_set_state_changed_handler(_listener,^(nw_listener_state_t state,nw_error_t error) {
        if(state==nw_listener_state_failed) { NSLog(@"[IRB] Listener failed: %d",nw_error_get_error_code(error)); exit(1); }
        if(state==nw_listener_state_ready) NSLog(@"[IRB] TLS listener ready; capture requires local owner consent.");
    });
    nw_listener_set_new_connection_handler(_listener,^(nw_connection_t c) { [weak acceptNetwork:c]; });
    if(![self startLocal]) return NO;
    nw_listener_start(_listener);
    [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {
        (void)timer; uint64_t now=IRBNow();
        if(self->_connection&&((!self->_authenticated&&now-self->_connectedAt>10000000)||
           (self->_authenticated&&!self->_authorized&&now-self->_connectedAt>55000000)||
           (self->_authorized&&now-self->_lastActivity>15000000)||
           (self->_awaitingAck&&now-self->_videoSentAt>5000000))) [self disconnect:@"connection timed out"];
    }];
    return YES;
}
- (BOOL)startLocal {
    struct stat st;
    if(lstat(IRB_DIRECTORY.fileSystemRepresentation,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0077)) {
        NSLog(@"[IRB] Private state directory must be owned by mobile with mode 0700."); return NO;
    }
    _localSocket=socket(AF_UNIX,SOCK_STREAM,0);
    if(_localSocket<0) return NO;
    unlink(IRB_SOCKET.fileSystemRepresentation);
    struct sockaddr_un addr={0}; addr.sun_family=AF_UNIX;
    strlcpy(addr.sun_path,IRB_SOCKET.fileSystemRepresentation,sizeof(addr.sun_path));
    mode_t previous=umask(0077);
    int result=bind(_localSocket,(struct sockaddr *)&addr,sizeof(addr)); umask(previous);
    if(result||listen(_localSocket,2)) return NO;
    chmod(IRB_SOCKET.fileSystemRepresentation,0600);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        while(YES) {
            int fd=accept(self->_localSocket,NULL,NULL); if(fd<0) continue;
            uid_t uid; gid_t gid;
            if(getpeereid(fd,&uid,&gid)||uid!=getuid()) { close(fd); continue; }
            dispatch_sync(dispatch_get_main_queue(),^{
                if(self->_bridge) { close(fd); return; }
                self->_bridge=[[IRBLocalPeer alloc] initWithSocket:fd]; __weak IRBServer *weak=self;
                self->_bridge.received=^(irb_header h,NSData *data) { [weak fromBridge:h data:data]; };
                self->_bridge.disconnected=^{ IRBServer *s=weak; if(!s) return; s->_bridge=nil; [s disconnect:@"SpringBoard bridge disconnected"]; };
                [self->_bridge start];
            });
        }
    }); return YES;
}
- (void)acceptNetwork:(nw_connection_t)c {
    if(_connection||IRBNow()<_cooldown||!_bridge) { nw_connection_cancel(c); return; }
    _connection=c; ++_generation; _rx=0; _tx=0; _pendingBytes=0;
    _authenticated=NO; _authorized=NO; _awaitingAck=NO;
    _received=[NSMutableData data]; _connectedAt=IRBNow(); _lastActivity=_connectedAt;
    _commands=0; _rateStart=_connectedAt;
    uint64_t generation=_generation; __weak IRBServer *weak=self;
    nw_connection_set_queue(c,dispatch_get_main_queue());
    nw_connection_set_state_changed_handler(c,^(nw_connection_state_t state,nw_error_t error) {
        (void)error; IRBServer *s=weak; if(!s||s->_generation!=generation) return;
        if(state==nw_connection_state_ready) [s receive:generation];
        else if(state==nw_connection_state_failed||state==nw_connection_state_cancelled) [s disconnect:@"TLS connection ended"];
    }); nw_connection_start(c);
}
- (void)disconnect:(NSString *)reason {
    if(!_connection) return;
    nw_connection_t c=_connection; _connection=nil; ++_generation;
    _authenticated=NO; _authorized=NO; _awaitingAck=NO; _received=nil; _pendingBytes=0;
    _cooldown=IRBNow()+2000000;
    [_bridge send:IRB_STOP flags:0 pts:0 data:IRBJSON(@{@"reason":reason})];
    nw_connection_cancel(c); NSLog(@"[IRB] %@",reason);
}
- (void)send:(uint8_t)type flags:(uint8_t)flags pts:(uint64_t)pts data:(NSData *)data {
    if(!_connection) return;
    if(data.length>IRB_MAX_PAYLOAD||_pendingBytes+data.length>IRB_MAX_PAYLOAD+IRB_MAX_RESPONSE) { [self disconnect:@"outbound queue limit"]; return; }
    NSData *packet=IRBPacket(type,flags,++_tx,pts,data); _pendingBytes+=packet.length;
    uint64_t generation=_generation;
    dispatch_data_t bytes=dispatch_data_create(packet.bytes,packet.length,dispatch_get_main_queue(),^{ (void)packet; });
    nw_connection_send(_connection,bytes,NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT,false,^(nw_error_t error) {
        if(generation!=self->_generation) return;
        self->_pendingBytes-=packet.length;
        if(error) [self disconnect:@"TLS write failed"];
    });
}
- (void)receive:(uint64_t)generation {
    if(!_connection||generation!=_generation) return;
    nw_connection_receive(_connection,1,65536,^(dispatch_data_t content,nw_content_context_t context,bool complete,nw_error_t error) {
        (void)context;
        if(generation!=self->_generation) return;
        if(content) dispatch_data_apply(content,^bool(dispatch_data_t region,size_t offset,const void *p,size_t n) {
            (void)region; (void)offset; [self->_received appendBytes:p length:n]; return true;
        });
        [self parse];
        if(generation!=self->_generation) return;
        if(error||complete) [self disconnect:@"peer closed TLS stream"]; else [self receive:generation];
    });
}
- (void)parse {
    while(_received.length>=IRB_HEADER_SIZE&&_connection) {
        irb_header h;
        if(!irb_unpack(_received.bytes,_received.length,&h)||
           (!_authenticated&&h.type!=IRB_AUTH)||(_authenticated&&h.type!=IRB_CONTROL&&h.type!=IRB_PING&&h.type!=IRB_CREDIT)) {
            [self disconnect:@"invalid client frame"]; return;
        }
        if(_received.length<IRB_HEADER_SIZE+h.length) return;
        if(h.sequence!=++_rx) { [self disconnect:@"invalid frame sequence"]; return; }
        NSData *data=[_received subdataWithRange:NSMakeRange(IRB_HEADER_SIZE,h.length)];
        [_received replaceBytesInRange:NSMakeRange(0,IRB_HEADER_SIZE+h.length) withBytes:NULL length:0];
        uint64_t now=IRBNow(); _lastActivity=now;
        if(now-_rateStart>1000000) { _rateStart=now; _commands=0; }
        if(++_commands>300) { [self disconnect:@"command rate limit"]; return; }
        if(!_authenticated) {
            NSDictionary *auth=IRBDictionary(data);
            if(!tokenEqual(auth[@"token"],_config[@"token"])) { _cooldown=now+3000000; [self disconnect:@"authentication failed"]; return; }
            _authenticated=YES;
            [self send:IRB_RESPONSE flags:0 pts:0 data:IRBJSON(@{@"event":@"approval_required"})];
            BOOL control=[auth[@"control"] isKindOfClass:NSNumber.class]&&[auth[@"control"] boolValue];
            int fps=([auth[@"fps"] isKindOfClass:NSNumber.class]&&[auth[@"fps"] intValue]==60)?60:30;
            [_bridge send:IRB_START flags:0 pts:0 data:IRBJSON(@{@"control":@(control),@"fps":@(fps)})];
        } else if(h.type==IRB_PING) [self send:IRB_PONG flags:0 pts:h.pts_us data:data];
        else if(h.type==IRB_CREDIT) {
            if(!_authorized||!_awaitingAck||h.pts_us!=_ackPTS||h.length) { [self disconnect:@"invalid frame acknowledgement"]; return; }
            _awaitingAck=NO; [_bridge send:IRB_CREDIT flags:0 pts:h.pts_us data:nil];
        } else {
            NSDictionary *r=IRBDictionary(data);
            if(!_authorized||![r[@"op"] isKindOfClass:NSString.class]||![r[@"id"] isKindOfClass:NSNumber.class]) { [self disconnect:@"invalid or unapproved command"]; return; }
            if([r[@"op"] isEqual:@"disconnect"]) { [self disconnect:@"remote disconnected"]; return; }
            [_bridge send:IRB_CONTROL flags:0 pts:0 data:data];
        }
    }
}
- (void)fromBridge:(irb_header)h data:(NSData *)data {
    if(h.type==IRB_STOP) { [self disconnect:@"session stopped on iPhone"]; _cooldown=IRBNow()+30000000; return; }
    if(!_authenticated) return;
    if(h.type==IRB_RESPONSE) {
        NSDictionary *r=IRBDictionary(data);
        if([r[@"event"] isEqual:@"authorized"]) _authorized=YES;
        [self send:IRB_RESPONSE flags:0 pts:h.pts_us data:data];
    } else if(h.type==IRB_VIDEO&&_authorized) {
        if(_awaitingAck) { [self disconnect:@"bridge violated frame credit"]; return; }
        _awaitingAck=YES; _ackPTS=h.pts_us; _videoSentAt=IRBNow();
        [self send:IRB_VIDEO flags:h.flags pts:h.pts_us data:data];
    }
}
@end
int main(void) {
    @autoreleasepool {
        signal(SIGPIPE,SIG_IGN);
        IRBServer *server=[IRBServer new];
        if(![server start]) return 78;
        [NSRunLoop.mainRunLoop run];
    } return 0;
}
