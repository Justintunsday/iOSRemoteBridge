#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreImage/CoreImage.h>
#import <objc/message.h>
#import <sys/socket.h>
#import <sys/un.h>
#import <sys/utsname.h>
#import <unistd.h>
#import <sys/resource.h>
#import <mach/mach.h>
#import "../common/IRBLocalPeer.h"
#import "../common/IRBValidation.h"
#import "../capture/IRBCapture.h"
#import "../encoder/IRBEncoder.h"
#import "../input/IRBInput.h"
#import <dlfcn.h>
#include "../../shared/irb_frame_window.h"

@interface IRBBridge : NSObject
- (void)start;
@end
@implementation IRBBridge {
    IRBLocalPeer *_peer;
    IRBCapture *_capture;
    IRBEncoder *_encoder;
    IRBInput *_input;
    CADisplayLink *_display;
    id _axSettings;
    BOOL _assistiveChanged,_menuChanged;
    NSMutableDictionary<NSNumber *,NSDictionary *> *_nativeRequests;
    uint64_t _session;
    CIContext *_ci;
    CVPixelBufferRef _lastFrame;
    BOOL _connecting,_approved,_control,_forceKey;
    BOOL _snapshotPending,_keyboardEnabled;
    uint64_t _deadline,_generation,_frames,_skipped,_started,_lastVideoPTS;
    irb_frame_window _videoWindow;
    unsigned _windowLimit;
    NSMutableSet<NSNumber *> *_encodingPTS;
    double _captureMs,_encodeMs,_scale;
    int _fps,_bitrate,_failures,_slow;
    dispatch_source_t _gesture;
    NSDictionary *_pendingConfiguration;
}
- (void)status:(NSString *)state extra:(NSDictionary *)extra {
    NSMutableDictionary *result=[@{@"state":state,@"updated":@([NSDate.date timeIntervalSince1970]),
        @"scene_count":@(UIApplication.sharedApplication.connectedScenes.count),@"locked":@([self locked])} mutableCopy];
    if(extra) [result addEntriesFromDictionary:extra];
    NSString *path=IRB_DIRECTORY @"/bridge-status.json";
    [IRBJSON(result) writeToFile:path atomically:YES];
    [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:path error:NULL];
}
- (BOOL)locked {
    Class cls=NSClassFromString(@"SBLockScreenManager");
    SEL shared=NSSelectorFromString(@"sharedInstance"), locked=NSSelectorFromString(@"isUILocked");
    if(![cls respondsToSelector:shared]) return YES;
    id manager=((id(*)(id,SEL))objc_msgSend)(cls,shared);
    if(![manager respondsToSelector:locked]) return YES;
    return ((BOOL(*)(id,SEL))objc_msgSend)(manager,locked);
}
- (void)start {
    _input=[IRBInput new];
    [self status:@"loaded" extra:@{@"input_status":_input.status}];
    [NSTimer scheduledTimerWithTimeInterval:3 repeats:YES block:^(NSTimer *timer) {
        (void)timer; [self connect];
        if(self->_approved&&([self locked]||IRBNow()>self->_deadline)) [self stopSession:@"locked or session expired"];
    }];
    [self connect];
}
- (void)connect {
    if(_peer||_connecting) return; _connecting=YES;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
        int fd=socket(AF_UNIX,SOCK_STREAM,0); struct sockaddr_un addr={0};
        addr.sun_family=AF_UNIX; strlcpy(addr.sun_path,IRB_SOCKET.fileSystemRepresentation,sizeof(addr.sun_path));
        BOOL ok=fd>=0&&connect(fd,(struct sockaddr *)&addr,sizeof(addr))==0;
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_connecting=NO;
            if(!ok) { if(fd>=0) close(fd); return; }
            self->_peer=[[IRBLocalPeer alloc] initWithSocket:fd];
            __weak IRBBridge *weak=self;
            self->_peer.received=^(irb_header h,NSData *data) { [weak received:h data:data]; };
            self->_peer.disconnected=^{ IRBBridge *s=weak; if(!s) return; [s stopSession:@"daemon disconnected"]; s->_peer=nil; };
            [self->_peer start];
        });
    });
}
- (void)send:(NSDictionary *)object { [_peer send:IRB_RESPONSE flags:0 pts:0 data:IRBJSON(object)]; }
- (void)reply:(NSDictionary *)request result:(id)result error:(NSString *)error {
    [self send:IRBReply(request,result,error)];
}
- (BOOL)prepareSystemPointer {
    if(!_axSettings) {
        dlopen("/System/Library/PrivateFrameworks/AccessibilityUtilities.framework/AccessibilityUtilities",RTLD_NOW);
        Class cls=NSClassFromString(@"AXSettings"); SEL shared=NSSelectorFromString(@"sharedInstance");
        if([cls respondsToSelector:shared]) _axSettings=((id(*)(id,SEL))objc_msgSend)(cls,shared);
    }
    SEL get=NSSelectorFromString(@"assistiveTouchEnabled"),set=NSSelectorFromString(@"setAssistiveTouchEnabled:");
    if(![_axSettings respondsToSelector:get]||![_axSettings respondsToSelector:set]) return NO;
    if(!((BOOL(*)(id,SEL))objc_msgSend)(_axSettings,get)) {
        _assistiveChanged=YES; ((void(*)(id,SEL,BOOL))objc_msgSend)(_axSettings,set,YES);
    }
    SEL menu=NSSelectorFromString(@"assistiveTouchAlwaysShowMenu"),setMenu=NSSelectorFromString(@"setAssistiveTouchAlwaysShowMenu:");
    if([_axSettings respondsToSelector:menu]&&[_axSettings respondsToSelector:setMenu]&&
       ((BOOL(*)(id,SEL))objc_msgSend)(_axSettings,menu)) {
        _menuChanged=YES; ((void(*)(id,SEL,BOOL))objc_msgSend)(_axSettings,setMenu,NO);
    }
    return YES;
}
- (void)restoreSystemPointer {
    // Restore settings that this session changed; retain the owner's existing
    // AssistiveTouch configuration and do not overwrite later manual changes.
    SEL get=NSSelectorFromString(@"assistiveTouchEnabled"),set=NSSelectorFromString(@"setAssistiveTouchEnabled:");
    if(_assistiveChanged&&((BOOL(*)(id,SEL))objc_msgSend)(_axSettings,get))
        ((void(*)(id,SEL,BOOL))objc_msgSend)(_axSettings,set,NO);
    SEL menu=NSSelectorFromString(@"assistiveTouchAlwaysShowMenu"),setMenu=NSSelectorFromString(@"setAssistiveTouchAlwaysShowMenu:");
    if(_menuChanged&&!((BOOL(*)(id,SEL))objc_msgSend)(_axSettings,menu))
        ((void(*)(id,SEL,BOOL))objc_msgSend)(_axSettings,setMenu,YES);
    _assistiveChanged=_menuChanged=NO;
}
- (void)nativeRequest:(NSDictionary *)r {
    if(!_nativeRequests) _nativeRequests=[NSMutableDictionary new];
    if(_nativeRequests.count>=256||_nativeRequests[r[@"id"]]) {
        [self reply:r result:nil error:@"native HID request queue full or duplicate id"]; return;
    }
    _nativeRequests[r[@"id"]]=r;
    [_peer send:IRB_HID flags:0 pts:_session data:IRBJSON(r)];
}
- (void)stopSession:(NSString *)reason {
    BOOL was=_approved;
    [self status:@"stopped" extra:@{@"reason":reason}];
    _approved=NO; _keyboardEnabled=NO; ++_generation;
    [_nativeRequests removeAllObjects]; [self restoreSystemPointer];
    [_display invalidate]; _display=nil;
    if(_gesture) { dispatch_source_cancel(_gesture); _gesture=nil; }
    [_input releaseAll]; [_encoder stop]; _encoder=nil; _capture=nil;
    if(_lastFrame) { CVPixelBufferRelease(_lastFrame); _lastFrame=NULL; }
    [_encodingPTS removeAllObjects]; irb_window_init(&_videoWindow,1);
    UIDevice.currentDevice.batteryMonitoringEnabled=NO;
    _pendingConfiguration=nil;
    _ci=nil; _snapshotPending=NO;
    if(was) [_peer send:IRB_STOP flags:0 pts:0 data:IRBJSON(@{@"reason":reason})];
}
- (BOOL)startPipeline {
    _capture=[IRBCapture new]; _encoder=[IRBEncoder new];
    if(![_capture prepareWithScale:_scale]||![_encoder startWidth:_capture.width height:_capture.height fps:_fps bitrate:_bitrate]) {
        [self send:@{@"event":@"error",@"error":_capture.error?:_encoder.error?:@"pipeline failed"}]; return NO;
    }
    uint64_t generation=_generation; __weak IRBBridge *weak=self;
    _encoder.output=^(NSData *data,BOOL key,uint64_t pts,double ms,OSStatus error) {
        dispatch_async(dispatch_get_main_queue(),^{
            IRBBridge *s=weak; if(!s||!s->_approved||generation!=s->_generation) return;
            s->_encodeMs=ms;
            if(error||!data.length) {
                [s stopSession:@"encoder output failed; reconnect to recreate the bounded pipeline"];
                return;
            }
            if(![s->_encodingPTS containsObject:@(pts)]||pts<=s->_lastVideoPTS) {
                [s stopSession:@"encoder output timestamp order violated"]; return;
            }
            [s->_encodingPTS removeObject:@(pts)]; s->_lastVideoPTS=pts;
            s->_failures=0; ++s->_frames;
            [s->_peer send:IRB_VIDEO flags:key?1:0 pts:pts data:data];
        });
    };
    irb_window_init(&_videoWindow,_windowLimit);
    _encodingPTS=[NSMutableSet set]; _lastVideoPTS=0;
    _forceKey=YES; _failures=0; _slow=0;
    _started=IRBNow(); _frames=0; _skipped=0;
    UIDevice.currentDevice.batteryMonitoringEnabled=YES;
    _display=[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    _display.preferredFramesPerSecond=_fps;
    [_display addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    [self status:@"streaming" extra:[self info]];
    return YES;
}
- (void)tick:(CADisplayLink *)link {
    (void)link;
    if(!_approved) return;
    if([self locked]||IRBNow()>_deadline) { [self stopSession:@"locked or session expired"]; return; }
    NSProcessInfo *process=NSProcessInfo.processInfo;
    if(process.thermalState==NSProcessInfoThermalStateCritical) { [self stopSession:@"critical thermal pressure"]; return; }
    int effective=(process.thermalState>=NSProcessInfoThermalStateSerious||process.lowPowerModeEnabled)?MIN(_fps,30):_fps;
    if(_display.preferredFramesPerSecond!=effective) {
        if(![_encoder setFPS:effective]) { [self stopSession:@"frame-rate update failed"]; return; }
        _display.preferredFramesPerSecond=effective;
    }
    for(NSNumber *pts in _encodingPTS) if(IRBNow()-pts.unsignedLongLongValue>2000000) {
        [self stopSession:@"encoder callback timed out"]; return;
    }
    if(!irb_window_available(&_videoWindow)||_encodingPTS.count>=MIN(_videoWindow.limit,2u)||_pendingConfiguration) { ++_skipped; return; }
    uint64_t start=IRBNow(); CVPixelBufferRef frame=[_capture copyFrame]; _captureMs=(IRBNow()-start)/1000.0;
    if(!frame) { if(++_failures>=3) [self stopSession:_capture.error?:@"capture failed"]; return; }
    _slow=_captureMs>25?_slow+1:0;
    if(_slow>=5) { CVPixelBufferRelease(frame); [self stopSession:@"capture main-thread budget exceeded"]; return; }
    if(_lastFrame) CVPixelBufferRelease(_lastFrame); _lastFrame=frame;
    if(!irb_window_push(&_videoWindow,start,IRBNow())) { [self stopSession:@"capture credit invariant failed"]; return; }
    [_encodingPTS addObject:@(start)];
    OSStatus error=[_encoder encode:frame pts:start forceKeyframe:_forceKey]; _forceKey=NO;
    if(error) {
        [self stopSession:@"encoder submission failed; reconnect to recover"];
    }
}
- (NSDictionary *)info {
    struct utsname u; uname(&u);
    return @{@"model":[NSString stringWithUTF8String:u.machine],@"ios":UIDevice.currentDevice.systemVersion,
        @"width":@(_capture.width),@"height":@(_capture.height),@"fps_requested":@(_fps),
        @"video_window":@(_videoWindow.limit),
        @"control":@(_control),@"keyboard_enabled":IRBBool(_keyboardEnabled),@"keyboard_backend":@"IOHIDUserDevice",@"approval_policy":@"paired_client_automatic",@"orientation":@"natural_portrait",@"input_status":_input.status,
        @"input_diagnostics":_input.diagnostics,
        @"hardware_encoder":_encoder.hardwareStatus?:@"unknown",@"low_latency_mode":@(_encoder.lowLatency),
        @"runtime_verified":@NO};
}
- (void)received:(irb_header)h data:(NSData *)data {
    if(h.type==IRB_HID) {
        if(!_approved||h.pts_us!=_session) return;
        NSDictionary *reply=IRBDictionary(data); NSNumber *ident=IRBNumber(reply[@"id"]);
        NSDictionary *request=ident?_nativeRequests[ident]:nil; if(!request) return;
        [_nativeRequests removeObjectForKey:ident];
        if([request[@"op"] isEqual:@"keyboard_mode"]) {
            _keyboardEnabled=[reply[@"ok"] boolValue]&&[reply[@"result"][@"keyboard_enabled"] boolValue];
            if(!_keyboardEnabled) [self restoreSystemPointer];
        }
        [self send:reply]; return;
    }
    if(h.type==IRB_STOP) { [self stopSession:@"connection ended"]; return; }
    if(h.type==IRB_CREDIT) {
        if(!_approved) return;
        if(h.length||!irb_window_ack(&_videoWindow,h.pts_us)) { [self stopSession:@"invalid local frame acknowledgement"]; return; }
        if(_pendingConfiguration&&!_videoWindow.count&&!_encodingPTS.count) {
            NSDictionary *r=_pendingConfiguration; _pendingConfiguration=nil;
            [self applyConfiguration:r];
        }
        return;
    }
    if(h.type==IRB_START) {
        if(_approved) return;
        if([self locked]) {
            [_peer send:IRB_STOP flags:0 pts:0 data:IRBJSON(@{@"reason":@"unlock the iPhone before connecting"})]; return;
        }
        NSDictionary *options=IRBDictionary(data);
        _fps=[IRBNumber(options[@"fps"]) intValue]==60?60:30;
        unsigned requested=[IRBNumber(options[@"video_window"]) unsignedIntValue];
        _windowLimit=requested>=1&&requested<=IRB_MAX_VIDEO_WINDOW?requested:1;
        _session=[IRBNumber(options[@"session"]) unsignedLongLongValue];
        _bitrate=8000000; _scale=1; _control=[IRBNumber(options[@"control"]) boolValue];
        ++_generation; _approved=YES; _deadline=IRBNow()+900000000ULL;
        if(![self startPipeline]) { [self stopSession:@"pipeline failed"]; return; }
        [self status:@"paired_session_started" extra:@{@"control":IRBBool(_control),@"approval_policy":@"paired_client_automatic"}];
        [self send:@{@"event":@"authorized",@"device":[self info]}];
        return;
    }
    if(h.type!=IRB_CONTROL||!_approved) return;
    NSDictionary *r=IRBDictionary(data); NSString *op=r[@"op"];
    if(![op isKindOfClass:NSString.class]||![r[@"id"] isKindOfClass:NSNumber.class]) return;
    if(!IRBValidateRequest(r,YES)) { [self reply:r result:nil error:@"invalid operation arguments"]; return; }
    if([op isEqual:@"get_device_info"]||[op isEqual:@"get_orientation"]||[op isEqual:@"get_connection_status"]) {
        [self reply:r result:[self info] error:nil]; return;
    }
    if([op isEqual:@"get_performance_stats"]) {
        struct rusage usage={0}; getrusage(RUSAGE_SELF,&usage);
        mach_task_basic_info_data_t memory={0}; mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
        BOOL haveMemory=task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&memory,&count)==KERN_SUCCESS;
        [self reply:r result:@{@"capture_ms_last":@(_captureMs),@"encode_ms_last":@(_encodeMs),
             @"encoded_frames":@(_frames),@"backpressure_skips":@(_skipped),@"bitrate":@(_bitrate),
             @"video_window":@(_videoWindow.limit),@"frames_in_flight":@(_videoWindow.count),@"pending_encodes":@(_encodingPTS.count),
             @"fps_paced":@(_display.preferredFramesPerSecond),
             @"thermal_state":@(NSProcessInfo.processInfo.thermalState),
             @"low_power_mode":@(NSProcessInfo.processInfo.lowPowerModeEnabled),
             @"battery_level":@(UIDevice.currentDevice.batteryLevel),
             @"springboard_resident_bytes":haveMemory?@(memory.resident_size):[NSNull null],
             @"springboard_cpu_user_seconds":@(usage.ru_utime.tv_sec+usage.ru_utime.tv_usec/1000000.0),
             @"springboard_cpu_system_seconds":@(usage.ru_stime.tv_sec+usage.ru_stime.tv_usec/1000000.0),
             @"encoded_fps_mean":@(_frames*1000000.0/MAX(1ULL,IRBNow()-_started)),
             @"glass_to_glass_ms":[NSNull null],@"hardware_encoder":_encoder.hardwareStatus?:@"unknown"} error:nil]; return;
    }
    if([op isEqual:@"get_screen_frame"]) {
        [self snapshot:r]; return;
    }
    if([op isEqual:@"configure"]) {
        int rate=[IRBNumber(r[@"bitrate"]) intValue];
        if(rate<500000||rate>20000000) { [self reply:r result:nil error:@"bitrate must be 500000..20000000"]; return; }
        if(r[@"fps"]||r[@"scale"]) {
            int fps=[IRBNumber(r[@"fps"]) intValue]; double scale=IRBReal(r[@"scale"]);
            if((fps!=30&&fps!=60)||!isfinite(scale)||scale<.25||scale>1) { [self reply:r result:nil error:@"fps 30/60 and scale .25..1 required"]; return; }
            if(_pendingConfiguration) { [self reply:r result:nil error:@"quality change already pending"]; return; }
            if(_encodingPTS.count||_videoWindow.count) { _pendingConfiguration=[r copy]; return; }
            [self applyConfiguration:r]; return;
        } else if(![_encoder setBitrate:rate]) { [self reply:r result:nil error:_encoder.error]; return; }
        _bitrate=rate; [self reply:r result:[self info] error:nil]; return;
    }
    if([op isEqual:@"request_keyframe"]) { _forceKey=YES; [self reply:r result:@{} error:nil]; return; }
    if([op isEqual:@"keyboard_mode"]&&![r[@"enabled"] boolValue]) {
        _keyboardEnabled=NO; [_input releaseKeys]; [self nativeRequest:r]; return;
    }
    if(!_control||[self locked]) { [self reply:r result:nil error:@"input disabled or phone locked"]; return; }
    if([op isEqual:@"key_event"]||[op isEqual:@"mouse_event"]) {
        if(!_keyboardEnabled) { [self reply:r result:nil error:@"enable System mouse+keyboard (F10) first"]; return; }
        [self nativeRequest:r]; return;
    }
    if(_gesture) { [self reply:r result:nil error:@"gesture busy"]; return; }
    if([op isEqual:@"keyboard_mode"]) {
        [_input releaseAll];
        if(![self prepareSystemPointer]) {
            [self reply:r result:nil error:@"AssistiveTouch settings API unavailable on this iOS version"]; return;
        }
        [self nativeRequest:r]; return;
    }
    [self execute:r];
}
- (void)execute:(NSDictionary *)r {
    NSString *op=r[@"op"]; BOOL ok=NO;
    double x=IRBReal(r[@"x"]),y=IRBReal(r[@"y"]);
    if([op isEqual:@"touch"]) {
        if(![r[@"finger"] isKindOfClass:NSNumber.class]||![r[@"phase"] isKindOfClass:NSNumber.class]||
           ![r[@"x"] isKindOfClass:NSNumber.class]||![r[@"y"] isKindOfClass:NSNumber.class]) { [self reply:r result:nil error:@"touch requires finger, phase, x, y numbers"]; return; }
        ok=[_input touch:[r[@"finger"] intValue] phase:[r[@"phase"] intValue] x:x y:y];
    } else if([op isEqual:@"tap"]||[op isEqual:@"double_tap"]||[op isEqual:@"long_press"]||[op isEqual:@"swipe"]) {
        [self gesture:r]; return;
    } else if([op isEqual:@"type_text"]) {
        [self typeText:r]; return;
    } else if([op isEqual:@"key"]) {
        NSNumber *usage=IRBNumber(r[@"usage"]);
        if(usage&&usage.doubleValue==usage.intValue&&usage.intValue>=4&&usage.intValue<=231)
            ok=[_input key:usage.unsignedShortValue down:YES]&&[_input key:usage.unsignedShortValue down:NO];
    } else if([op isEqual:@"press_home"]) ok=[_input button:@"home"];
    else if([op isEqual:@"volume_up"]||[op isEqual:@"volume_down"]) ok=[_input button:op];
    else if([op isEqual:@"app_switcher"]) {
        ok=[_input openAppSwitcher];
    } else if([op isEqual:@"control_center"]) {
        ok=[_input openControlCenter];
    } else if([op isEqual:@"launch_app"]) {
        NSString *bundle=r[@"bundle_id"];
        Class cls=NSClassFromString(@"LSApplicationWorkspace");
        SEL shared=NSSelectorFromString(@"defaultWorkspace"),launch=NSSelectorFromString(@"openApplicationWithBundleID:");
        if([bundle isKindOfClass:NSString.class]&&bundle.length<256&&[cls respondsToSelector:shared]) {
            id ws=((id(*)(id,SEL))objc_msgSend)(cls,shared);
            if([ws respondsToSelector:launch]) ok=((BOOL(*)(id,SEL,id))objc_msgSend)(ws,launch,bundle);
        }
    } else { [self reply:r result:nil error:@"unsupported operation"]; return; }
    if(![op isEqual:@"touch"]||[r[@"phase"] intValue]==2)
        [self status:@"input_result" extra:@{@"operation":op,@"submitted":@(ok),@"input_diagnostics":_input.diagnostics}];
    [self reply:r result:@{@"state":@"submitted",@"delivery_verified":@NO} error:ok?nil:@"HID unavailable, invalid arguments, unsupported ASCII, or private API refused operation"];
}
- (void)applyConfiguration:(NSDictionary *)r {
    [_display invalidate]; _display=nil; [_encoder stop];
    if(_lastFrame) { CVPixelBufferRelease(_lastFrame); _lastFrame=NULL; }
    _fps=[IRBNumber(r[@"fps"]) intValue]; _scale=IRBReal(r[@"scale"]); _bitrate=[IRBNumber(r[@"bitrate"]) intValue];
    if(![self startPipeline]) { [self stopSession:@"reconfiguration failed"]; return; }
    [self reply:r result:[self info] error:nil];
}
- (void)snapshot:(NSDictionary *)r {
    if(!_lastFrame||_snapshotPending) { [self reply:r result:nil error:@"frame unavailable or snapshot busy"]; return; }
    _snapshotPending=YES;
    if(!_ci) _ci=[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@NO,kCIContextCacheIntermediates:@NO}];
    CIContext *context=_ci;
    CVPixelBufferRef buffer=CVPixelBufferRetain(_lastFrame);
    int width=(int)CVPixelBufferGetWidth(buffer),height=(int)CVPixelBufferGetHeight(buffer);
    uint64_t generation=_generation;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{ @autoreleasepool {
        CIImage *image=[CIImage imageWithCVPixelBuffer:buffer];
        CGImageRef cg=[context createCGImage:image fromRect:image.extent];
        NSData *jpeg=cg?UIImageJPEGRepresentation([UIImage imageWithCGImage:cg],.65):nil;
        if(cg) CGImageRelease(cg);
        CVPixelBufferRelease(buffer);
        NSString *encoded=jpeg.length<=1400000?[jpeg base64EncodedStringWithOptions:0]:nil;
        dispatch_async(dispatch_get_main_queue(),^{
            if(generation!=self->_generation||!self->_approved) return;
            self->_snapshotPending=NO;
            if(!encoded) { [self reply:r result:nil error:@"JPEG unavailable or exceeds response limit"]; return; }
            [self reply:r result:@{@"mime":@"image/jpeg",@"base64":encoded,@"width":@(width),
                 @"height":@(height),@"orientation":@"natural_portrait"} error:nil];
        });
    }});
}
- (void)typeText:(NSDictionary *)r {
    NSString *text=r[@"text"];
    if(![text isKindOfClass:NSString.class]||![_input supportsText:text]||!_input.ready) {
        [self reply:r result:nil error:@"text requires supported US-layout ASCII (maximum 1024 characters) and ready HID"]; return;
    }
    if(!text.length) { [self reply:r result:@{} error:nil]; return; }
    __block NSUInteger index=0;
    _gesture=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
    dispatch_source_set_timer(_gesture,DISPATCH_TIME_NOW,10*NSEC_PER_MSEC,NSEC_PER_MSEC);
    dispatch_source_set_event_handler(_gesture,^{
        BOOL ok=[self->_input typeASCII:[text substringWithRange:NSMakeRange(index++,1)]];
        if(!ok||index>=text.length) {
            dispatch_source_cancel(self->_gesture); self->_gesture=nil;
            if(!ok) [self->_input releaseAll];
            [self reply:r result:@{@"state":@"completed",@"delivery_verified":@NO} error:ok?nil:@"HID text injection failed"];
        }
    }); dispatch_resume(_gesture);
}
- (void)gesture:(NSDictionary *)r {
    NSString *op=r[@"op"]; BOOL swipe=[op isEqual:@"swipe"],twice=[op isEqual:@"double_tap"];
    double x=IRBReal(r[@"x"]),y=IRBReal(r[@"y"]);
    double x2=swipe?IRBReal(r[@"to_x"]):x,y2=swipe?IRBReal(r[@"to_y"]):y;
    double duration=([op isEqual:@"long_press"]||swipe)?IRBReal(r[@"duration_ms"])/1000.:.06;
    double unusedX,unusedY;
    if(![r[@"x"] isKindOfClass:NSNumber.class]||![r[@"y"] isKindOfClass:NSNumber.class]||
       (swipe&&(![r[@"to_x"] isKindOfClass:NSNumber.class]||![r[@"to_y"] isKindOfClass:NSNumber.class]))||
       !irb_unrotate(x,y,0,&unusedX,&unusedY)||!irb_unrotate(x2,y2,0,&unusedX,&unusedY)||
       !isfinite(duration)||duration<.04||duration>5||![_input touch:0 phase:0 x:x y:y]) {
        [self reply:r result:nil error:@"invalid gesture or HID not ready"]; return;
    }
    uint64_t start=IRBNow(); __block BOOL lifted=NO,second=NO;
    _gesture=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
    dispatch_source_set_timer(_gesture,dispatch_time(DISPATCH_TIME_NOW,16*NSEC_PER_MSEC),16*NSEC_PER_MSEC,NSEC_PER_MSEC);
    dispatch_source_set_event_handler(_gesture,^{
        double t=(IRBNow()-start)/1000000.;
        if(swipe&&t<duration) [self->_input touch:0 phase:1 x:x+(x2-x)*t/duration y:y+(y2-y)*t/duration];
        if(t>=duration&&!lifted) { [self->_input touch:0 phase:2 x:x2 y:y2]; lifted=YES; }
        if(twice&&t>=.16&&!second) { [self->_input touch:0 phase:0 x:x y:y]; second=YES; }
        if(twice&&t>=.22) [self->_input touch:0 phase:2 x:x y:y];
        if(t>=(twice?.22:duration)) {
            dispatch_source_cancel(self->_gesture); self->_gesture=nil;
            [self reply:r result:@{@"state":@"completed",@"delivery_verified":@NO} error:nil];
        }
    }); dispatch_resume(_gesture);
}
@end
__attribute__((constructor)) static void loadBridge(void) {
    @autoreleasepool {
        if(![NSProcessInfo.processInfo.processName isEqual:@"SpringBoard"]) return;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            static IRBBridge *bridge; bridge=[IRBBridge new]; [bridge start];
        });
    }
}
