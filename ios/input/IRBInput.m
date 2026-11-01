// IOHID declarations/dispatch pattern informed by ScreenMirror (MIT).
// No ZXTouch GPL implementation is incorporated. See THIRD_PARTY_NOTICES.md.
#import "IRBInput.h"
#import <dlfcn.h>
#import <mach/mach_time.h>
typedef CFTypeRef Event;
typedef CFTypeRef Client;
typedef double HIDFloat;
typedef void (*EventCallback)(void *,void *,void *,Event);
typedef Client (*CreateClient)(CFAllocatorRef);
typedef void (*Dispatch)(Client,Event);
typedef Event (*Digitizer)(CFAllocatorRef,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,
                         HIDFloat,HIDFloat,HIDFloat,HIDFloat,HIDFloat,Boolean,Boolean,uint32_t);
typedef Event (*Finger)(CFAllocatorRef,uint64_t,uint32_t,uint32_t,uint32_t,HIDFloat,HIDFloat,HIDFloat,
                       HIDFloat,HIDFloat,Boolean,Boolean,uint32_t);
typedef Event (*Keyboard)(CFAllocatorRef,uint64_t,uint32_t,uint32_t,Boolean,uint32_t);
typedef void (*SetInt)(Event,uint32_t,CFIndex);
typedef void (*SetSender)(Event,uint64_t);
typedef uint64_t (*GetSender)(Event);
typedef uint32_t (*GetType)(Event);
typedef void (*Append)(Event,Event,uint32_t);
typedef void (*Schedule)(Client,CFRunLoopRef,CFStringRef);
typedef void (*Register)(Client,EventCallback,void *,void *);
@interface IRBInput ()
- (void)observe:(Event)event;
@end
@implementation IRBInput {
    Client _client;
    Dispatch _dispatch; Digitizer _digitizer; Finger _finger; Keyboard _keyboard;
    SetInt _setInt; SetSender _setSender; GetSender _getSender; GetType _getType; Append _append;
    uint64_t _sender;
    BOOL _down[10],_keys[256]; CGPoint _positions[10];
}
static void observed(void *target,void *ref,void *service,Event event) {
    (void)ref; (void)service; [(__bridge IRBInput *)target observe:event];
}
- (instancetype)init {
    if((self=[super init])) {
        void *h=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW);
        if(!h) h=dlopen("/System/Library/PrivateFrameworks/IOKit.framework/IOKit",RTLD_NOW);
        if(!h) return self;
#define RESOLVE(type,name) (type)dlsym(h,name)
        CreateClient create=RESOLVE(CreateClient,"IOHIDEventSystemClientCreate");
        _dispatch=RESOLVE(Dispatch,"IOHIDEventSystemClientDispatchEvent");
        _digitizer=RESOLVE(Digitizer,"IOHIDEventCreateDigitizerEvent");
        _finger=RESOLVE(Finger,"IOHIDEventCreateDigitizerFingerEvent");
        _keyboard=RESOLVE(Keyboard,"IOHIDEventCreateKeyboardEvent");
        _append=RESOLVE(Append,"IOHIDEventAppendEvent");
        _setInt=RESOLVE(SetInt,"IOHIDEventSetIntegerValue");
        _setSender=RESOLVE(SetSender,"IOHIDEventSetSenderID");
        _getSender=RESOLVE(GetSender,"IOHIDEventGetSenderID");
        _getType=RESOLVE(GetType,"IOHIDEventGetType");
        Schedule schedule=RESOLVE(Schedule,"IOHIDEventSystemClientScheduleWithRunLoop");
        Register reg=RESOLVE(Register,"IOHIDEventSystemClientRegisterEventCallback");
        if(create) _client=create(kCFAllocatorDefault);
        if(_client&&schedule&&reg&&_getSender&&_getType) {
            schedule(_client,CFRunLoopGetMain(),kCFRunLoopCommonModes);
            reg(_client,observed,(__bridge void *)self,NULL);
        }
#undef RESOLVE
    } return self;
}
- (void)observe:(Event)e {
    if(!_sender&&_getType(e)==11) _sender=_getSender(e); // kIOHIDEventTypeDigitizer
}
- (BOOL)ready { return _client&&_dispatch&&_digitizer&&_finger&&_append&&_setInt&&_setSender&&_sender; }
- (NSString *)status { return self.ready?@"symbols resolved; delivery unverified":@"waiting for HID APIs and a physical touchscreen event"; }
- (BOOL)touch:(int)index phase:(int)phase x:(double)x y:(double)y {
    if(!self.ready||index<0||index>=10||phase<0||phase>2||!isfinite(x)||!isfinite(y)||x<0||x>1||y<0||y>1) return NO;
    if((phase==0&&_down[index])||(phase!=0&&!_down[index])) return NO;
    BOOL next=phase!=2, was=_down[index];
    _positions[index]=CGPointMake(x,y); _down[index]=next;
    BOOL any=NO; for(int i=0;i<10;++i) any|=_down[i];
    uint32_t mask=phase==1?4:3; uint64_t now=mach_absolute_time();
    Event parent=_digitizer(NULL,now,3,0,0,mask,0,x,y,0,0,0,any,any,0);
    if(!parent) { _down[index]=was; return NO; }
    _setInt(parent,0xb0019,1); _setSender(parent,_sender);
    BOOL ok=YES;
    for(int i=0;i<10;++i) if(_down[i]||i==index) {
        CGPoint p=_positions[i]; BOOL active=_down[i];
        Event child=_finger(NULL,now,(uint32_t)i+1,(uint32_t)i+2,i==index?mask:4,
                            p.x,p.y,0,active?0.5:0,0,active,active,0);
        if(!child) { ok=NO; break; }
        _setInt(child,0xb0019,1); _setSender(child,_sender); _append(parent,child,0); CFRelease(child);
    }
    if(ok) _dispatch(_client,parent); else _down[index]=was;
    CFRelease(parent); return ok;
}
- (BOOL)sendKey:(uint16_t)usage page:(uint16_t)page down:(BOOL)down {
    if(!_client||!_dispatch||!_keyboard||!_setSender||!_sender) return NO;
    Event e=_keyboard(NULL,mach_absolute_time(),page,usage,down,0);
    if(!e) return NO;
    _setSender(e,_sender); _dispatch(_client,e); CFRelease(e); return YES;
}
- (BOOL)key:(uint16_t)usage down:(BOOL)down {
    if(usage<4||usage>231) return NO;
    if([self sendKey:usage page:7 down:down]) { _keys[usage]=down; return YES; } return NO;
}
- (BOOL)button:(NSString *)button {
    uint16_t usage=0;
    if([button isEqual:@"home"]) usage=0x40;
    else if([button isEqual:@"volume_up"]) usage=0xe9;
    else if([button isEqual:@"volume_down"]) usage=0xea;
    if(!usage) return NO;
    return [self sendKey:usage page:12 down:YES]&&[self sendKey:usage page:12 down:NO];
}
static uint16_t asciiKey(unichar c,BOOL *shift) {
    *shift=NO;
    if(c>='a'&&c<='z') return 4+c-'a';
    if(c>='A'&&c<='Z') { *shift=YES; return 4+c-'A'; }
    if(c>='1'&&c<='9') return 0x1e+c-'1';
    if(c=='0') return 0x27;
    if(c=='\n'||c=='\r') return 0x28;
    if(c=='\t') return 0x2b;
    if(c==' ') return 0x2c;
    if(c==8||c==127) return 0x2a;
    NSString *plain=@"-=[]\\;'/`,.";
    const uint16_t codes[]={0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x38,0x35,0x36,0x37};
    NSString *shifted=@"_+{}|:\"?~<>";
    for(NSUInteger i=0;i<plain.length;i++) {
        if(c==[plain characterAtIndex:i]) return codes[i];
        if(c==[shifted characterAtIndex:i]) { *shift=YES; return codes[i]; }
    }
    NSString *digits=@"!@#$%^&*()";
    for(NSUInteger i=0;i<digits.length;++i) if(c==[digits characterAtIndex:i]) { *shift=YES; return i==9?0x27:(uint16_t)(0x1e+i); }
    return 0;
}
- (BOOL)typeASCII:(NSString *)text {
    if(text.length>1024) return NO;
    // Validate the whole string before any side effects; unsupported Unicode is an error.
    for(NSUInteger i=0;i<text.length;++i) { BOOL shift; if(!asciiKey([text characterAtIndex:i],&shift)) return NO; }
    for(NSUInteger i=0;i<text.length;++i) {
        BOOL shift; uint16_t code=asciiKey([text characterAtIndex:i],&shift);
        if(shift&&![self key:0xe1 down:YES]) return NO;
        BOOL ok=[self key:code down:YES]&&[self key:code down:NO];
        if(shift) [self key:0xe1 down:NO];
        if(!ok) { [self releaseAll]; return NO; }
    } return YES;
}
- (void)releaseAll {
    for(int i=0;i<10;++i) if(_down[i]) [self touch:i phase:2 x:_positions[i].x y:_positions[i].y];
    for(int i=4;i<256;++i) if(_keys[i]) [self key:(uint16_t)i down:NO];
}
/* Singleton lives for SpringBoard's lifetime. Do not deallocate a registered
   HID observer while its run-loop callback still references it. */
@end
