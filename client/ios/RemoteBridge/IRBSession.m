#import "IRBSession.h"
#import "IRBProtocol.h"
#import "IRBVideoDecoder.h"
#import <Network/Network.h>
#import <Security/Security.h>
#import <Security/SecProtocolOptions.h>
#import <CommonCrypto/CommonDigest.h>
#include <stdatomic.h>
@interface IRBOutbound : NSObject
@property(nonatomic) uint8_t type;
@property(nonatomic) uint64_t pts;
@property(nonatomic,strong,nullable) NSMutableDictionary *object;
@end
@implementation IRBOutbound
@end
@implementation IRBSession {
    dispatch_queue_t _queue;
    dispatch_source_t _timer;
    nw_connection_t _connection;
    IRBProfile *_profile;
    IRBPacketReader *_reader;
    IRBVideoDecoder *_decoder;
    NSMutableArray<IRBOutbound *> *_outgoing;
    NSMutableDictionary<NSNumber *,NSString *> *_pending;
    NSMutableArray<NSNumber *> *_inflight;
    _Atomic(uint64_t) _generation;
    uint32_t _tx,_request;
    uint64_t _connectedAt,_lastData,_statsAt,_lastKeyRequest,_frames;
    uint64_t _lastVideoPTS;
    BOOL _wantConnection,_authorized,_sending,_control;
    int _fps,_retries;
    double _rtt;
    NSString *_tlsFailure;
}
- (instancetype)init {
    if((self=[super init])) { atomic_init(&_generation,0);_queue=dispatch_queue_create("irb.controller.network",dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_USER_INTERACTIVE,0)); }
    return self;
}
- (uint64_t)generation { return atomic_load(&_generation); }
- (void)state:(IRBSessionState)state message:(NSString *)message {
    uint64_t generation=self.generation;
    dispatch_async(dispatch_get_main_queue(),^{ if(self.generation==generation&&self.onState) self.onState(state,message); });
}
- (void)clear {
    atomic_fetch_add(&_generation,1);
    if(_timer) { dispatch_source_cancel(_timer);_timer=nil; }
    if(_connection) { nw_connection_cancel(_connection);_connection=nil; }
    [_decoder invalidate];_decoder=nil;_reader=nil;
    _authorized=NO;_sending=NO;[_outgoing removeAllObjects];[_pending removeAllObjects];[_inflight removeAllObjects];
}
- (void)connect:(IRBProfile *)profile fps:(int)fps control:(BOOL)control {
    dispatch_async(_queue,^{
        [self clear];self->_profile=profile;self->_fps=fps==30?30:60;self->_control=control;self->_wantConnection=YES;self->_retries=0;[self attempt];
    });
}
- (void)disconnect { dispatch_async(_queue,^{self->_wantConnection=NO;[self clear];[self state:IRBSessionIdle message:@"已断开"];}); }
- (void)failed:(NSString *)message retry:(BOOL)retry {
    BOOL reconnect=retry&&_wantConnection;[self clear];
    if(!reconnect) { _wantConnection=NO;[self state:IRBSessionIdle message:message];return; }
    int delay=MIN(15,3*(1+_retries++));uint64_t generation=self.generation;
    [self state:IRBSessionConnecting message:[NSString stringWithFormat:@"%@ · %d 秒后重连",message,delay]];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,delay*NSEC_PER_SEC),_queue,^{if(self.generation==generation&&self->_wantConnection) [self attempt];});
}
- (void)attempt {
    if(!_wantConnection) return;
    atomic_fetch_add(&_generation,1);uint64_t generation=self.generation;
    _reader=[IRBPacketReader new];_decoder=[IRBVideoDecoder new];_outgoing=[NSMutableArray new];_pending=[NSMutableDictionary new];_inflight=[NSMutableArray new];_lastVideoPTS=0;
    _tx=_request=0;_frames=0;_rtt=0;_tlsFailure=nil;_sending=NO;_authorized=NO;
    _connectedAt=_lastData=_statsAt=IRBClientNow();_lastKeyRequest=0;
    [self state:IRBSessionConnecting message:@"正在连接，请保持被控 iPhone 解锁"];
    NSData *pin=_profile.pinBytes;__weak IRBSession *weak=self;
    nw_parameters_t parameters=nw_parameters_create_secure_tcp(^(nw_protocol_options_t tls) {
        sec_protocol_options_t options=nw_tls_copy_sec_protocol_options(tls);
        sec_protocol_options_set_min_tls_protocol_version(options,tls_protocol_version_TLSv12);
        sec_protocol_options_set_verify_block(options,^(sec_protocol_metadata_t metadata,sec_trust_t trust,sec_protocol_verify_complete_t complete) {
            (void)metadata;IRBSession *owner=weak;
            if(!owner||owner.generation!=generation) { complete(false);return; }
            SecTrustRef evaluation=sec_trust_copy_ref(trust);BOOL matches=NO,valid=NO;
            if(evaluation) {
                SecCertificateRef leaf=SecTrustGetCertificateAtIndex(evaluation,0);
                NSData *der=leaf?CFBridgingRelease(SecCertificateCopyData(leaf)):nil;
                if(der.length&&der.length<=65536) {
                    uint8_t digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(der.bytes,(CC_LONG)der.length,digest);
                    const uint8_t *expected=pin.bytes;volatile unsigned difference=0;
                    for(unsigned i=0;i<sizeof(digest);++i) difference|=digest[i]^expected[i];matches=difference==0;
                    if(matches) {
                        SecPolicyRef policy=SecPolicyCreateBasicX509();SecTrustSetPolicies(evaluation,policy);CFRelease(policy);
                        SecTrustSetAnchorCertificates(evaluation,(__bridge CFArrayRef)@[(__bridge id)leaf]);
                        SecTrustSetAnchorCertificatesOnly(evaluation,true);SecTrustSetNetworkFetchAllowed(evaluation,false);
                        valid=SecTrustEvaluateWithError(evaluation,NULL);
                    }
                }
                CFRelease(evaluation);
            }
            if(!matches) owner->_tlsFailure=@"证书与配对文件不匹配，未发送密钥";
            else if(!valid) owner->_tlsFailure=@"配对证书验证失败，未发送密钥";
            complete(matches&&valid);
        },self->_queue);
    },^(nw_protocol_options_t tcp) {nw_tcp_options_set_no_delay(tcp,true);});
    _connection=nw_connection_create(nw_endpoint_create_host(_profile.host.UTF8String,[NSString stringWithFormat:@"%u",_profile.port].UTF8String),parameters);
    nw_connection_set_queue(_connection,_queue);
    nw_connection_set_state_changed_handler(_connection,^(nw_connection_state_t state,nw_error_t error) {
        (void)error;IRBSession *owner=weak;if(!owner||owner.generation!=generation) return;
        if(state==nw_connection_state_ready) {
            IRBOutbound *auth=[IRBOutbound new];auth.type=IRB_AUTH;
            auth.object=[@{@"token":owner->_profile.token,@"control":owner->_control?@YES:@NO,@"fps":@(owner->_fps),@"video_window":@3} mutableCopy];
            [owner->_outgoing addObject:auth];[owner flush];[owner receive:generation];
        } else if(state==nw_connection_state_failed) [owner failed:owner->_tlsFailure?:@"连接失败，请检查地址、局域网权限及其他控制端是否已断开" retry:owner->_tlsFailure==nil];
    });
    _timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,_queue);
    dispatch_source_set_timer(_timer,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),NSEC_PER_SEC,20*NSEC_PER_MSEC);
    dispatch_source_set_event_handler(_timer,^{
        IRBSession *owner=weak;if(!owner||owner.generation!=generation) return;uint64_t now=IRBClientNow();
        if((!owner->_authorized&&now-owner->_connectedAt>45000000)||(owner->_authorized&&now-owner->_lastData>15000000)) { [owner failed:@"连接超时，请确认被控 iPhone 已解锁" retry:YES];return; }
        if(owner->_authorized) {
            [owner enqueue:IRB_PING pts:now];
            NSDictionary *stats=@{@"fps":@(owner->_frames*1000000.0/MAX(1ULL,now-owner->_statsAt)),@"rtt_ms":@(owner->_rtt)};
            owner->_frames=0;owner->_statsAt=now;
            dispatch_async(dispatch_get_main_queue(),^{if(owner.generation==generation&&owner.onStats) owner.onStats(stats);});
        }
    });dispatch_resume(_timer);nw_connection_start(_connection);
}
- (void)enqueue:(uint8_t)type pts:(uint64_t)pts {
    IRBOutbound *packet=[IRBOutbound new];packet.type=type;packet.pts=pts;[_outgoing addObject:packet];[self flush];
}
- (void)flush {
    if(_sending||!_connection||!_outgoing.count) return;
    if(_outgoing.count>128) { [self failed:@"发送队列已满，已断开以避免输入积压" retry:YES];return; }
    IRBOutbound *out=_outgoing.firstObject;[_outgoing removeObjectAtIndex:0];
    NSData *payload=out.object?[NSJSONSerialization dataWithJSONObject:out.object options:0 error:NULL]:nil;
    if(payload.length>IRB_MAX_CONTROL) { [self failed:@"操作数据过大" retry:NO];return; }
    NSData *packet=IRBClientPacket(out.type,++_tx,out.pts,payload);_sending=YES;uint64_t generation=self.generation;
    dispatch_data_t bytes=dispatch_data_create(packet.bytes,packet.length,_queue,^{(void)packet;});
    __weak IRBSession *weak=self;
    nw_connection_send(_connection,bytes,NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT,false,^(nw_error_t error) {
        IRBSession *owner=weak;if(!owner||owner.generation!=generation) return;
        owner->_sending=NO;if(error) [owner failed:@"连接已中断" retry:YES];else [owner flush];
    });
}
- (void)command:(NSDictionary *)request { dispatch_async(_queue,^{[self commandOnQueue:request];}); }
- (void)commandOnQueue:(NSDictionary *)request {
    if(!_authorized||!_connection) return;
    NSString *op=[request[@"op"] isKindOfClass:NSString.class]?request[@"op"]:nil;if(!op) return;
    IRBOutbound *last=_outgoing.lastObject;
    if([op isEqualToString:@"touch"]&&[request[@"phase"] intValue]==1&&last.type==IRB_CONTROL&&
       [last.object[@"op"] isEqual:@"touch"]&&[last.object[@"phase"] intValue]==1&&[last.object[@"finger"] isEqual:request[@"finger"]]) {
        NSNumber *ident=last.object[@"id"];last.object=[request mutableCopy];last.object[@"id"]=ident;return;
    }
    if(_pending.count>=128||_request>=INT32_MAX) { [self failed:@"操作响应积压，已断开连接" retry:YES];return; }
    IRBOutbound *packet=[IRBOutbound new];packet.type=IRB_CONTROL;packet.object=[request mutableCopy];
    NSNumber *ident=@(++_request);packet.object[@"id"]=ident;_pending[ident]=op;[_outgoing addObject:packet];[self flush];
}
- (void)receive:(uint64_t)generation {
    if(!_connection||self.generation!=generation) return;__weak IRBSession *weak=self;
    nw_connection_receive(_connection,1,65536,^(dispatch_data_t content,nw_content_context_t context,bool complete,nw_error_t error) {
        (void)context;IRBSession *owner=weak;if(!owner||owner.generation!=generation) return;
        if(content) {
            NSMutableData *bytes=[NSMutableData new];
            dispatch_data_apply(content,^bool(dispatch_data_t region,size_t offset,const void *buffer,size_t length) {(void)region;(void)offset;[bytes appendBytes:buffer length:length];return true;});
            owner->_lastData=IRBClientNow();
            for(IRBPacket *packet in [owner->_reader append:bytes]) { [owner handle:packet generation:generation];if(owner.generation!=generation) return; }
            if(owner->_reader.error) { [owner failed:owner->_reader.error retry:NO];return; }
        }
        if(error||complete) [owner failed:@"连接已中断，请保持被控手机解锁" retry:YES];else [owner receive:generation];
    });
}
- (void)handle:(IRBPacket *)packet generation:(uint64_t)generation {
    irb_header header=packet.header;
    if(header.type==IRB_PONG) { uint64_t now=IRBClientNow();if(header.pts_us<=now) _rtt=(now-header.pts_us)/1000.0;return; }
    if(header.type==IRB_RESPONSE) {
        NSDictionary *reply=IRBClientJSON(packet.payload);if(!reply) { [self failed:@"服务器响应格式不正确" retry:NO];return; }
        if([reply[@"event"] isEqual:@"authorized"]) {
            if(_authorized) { [self failed:@"重复的授权响应" retry:NO];return; }
            _authorized=YES;_retries=0;_frames=0;_statsAt=IRBClientNow();[self state:IRBSessionStreaming message:@"已连接"];
        }
        NSNumber *ident=[reply[@"id"] isKindOfClass:NSNumber.class]?reply[@"id"]:nil;
        if(ident&&ident.intValue>0) {
            if(!_pending[ident]) { [self failed:@"操作响应编号不正确" retry:NO];return; }[_pending removeObjectForKey:ident];
        }
        dispatch_async(dispatch_get_main_queue(),^{if(self.generation==generation&&self.onResponse) self.onResponse(reply);});return;
    }
    if(!_authorized) { [self failed:@"授权完成前收到视频" retry:NO];return; }
    if(_inflight.count>=3||header.pts_us<=_lastVideoPTS) { [self failed:@"视频超出帧窗口或顺序错误" retry:NO];return; }
    _lastVideoPTS=header.pts_us;[_inflight addObject:@(header.pts_us)];
    __weak IRBSession *weak=self;uint64_t pts=header.pts_us;
    [_decoder decode:packet.payload keyframe:(header.flags&1)!=0 completion:^(CVPixelBufferRef image,NSString *error) {
        IRBSession *owner=weak;if(!owner||owner.generation!=generation) return;
        if(image) CVPixelBufferRetain(image);
        dispatch_async(dispatch_get_main_queue(),^{
            if(owner.generation==generation&&image&&owner.onFrame) owner.onFrame(image,pts);
            if(image) CVPixelBufferRelease(image);
            dispatch_async(owner->_queue,^{
                if(owner.generation!=generation) return;
                if(!owner->_inflight.count||owner->_inflight.firstObject.unsignedLongLongValue!=pts) { [owner failed:@"视频解码确认顺序错误" retry:NO];return; }
                [owner->_inflight removeObjectAtIndex:0];
                if(!error) ++owner->_frames;
                [owner enqueue:IRB_CREDIT pts:pts];
                if(error&&IRBClientNow()-owner->_lastKeyRequest>1000000) { owner->_lastKeyRequest=IRBClientNow();[owner commandOnQueue:@{@"op":@"request_keyframe"}]; }
            });
        });
    }];
}
- (void)dealloc { if(_timer) dispatch_source_cancel(_timer);if(_connection) nw_connection_cancel(_connection); }
@end
