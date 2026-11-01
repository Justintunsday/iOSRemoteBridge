#import "IRBNativeHID.h"
#import "../common/IRBCommon.h"
#import <dlfcn.h>
#include "../../shared/irb_hid_reports.h"
typedef CFTypeRef HIDDevice;
typedef HIDDevice (*HIDCreate)(CFAllocatorRef,CFDictionaryRef);
typedef int32_t (*HIDReport)(HIDDevice,const uint8_t *,CFIndex);
typedef uint32_t (*HIDService)(HIDDevice);
typedef int32_t (*RegistryID)(uint32_t,uint64_t *);
typedef int32_t (*ObjectRelease)(uint32_t);
@implementation IRBNativeHID {
    HIDDevice _device;
    HIDCreate _create;
    HIDReport _report;
    HIDService _copyService;
    RegistryID _registryID;
    ObjectRelease _objectRelease;
    uint8_t _keys[256];
    uint64_t _keyboardReports,_pointerReports;
    NSString *_error;
}
- (instancetype)init {
    if((self=[super init])) {
        void *h=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW);
        if(!h) h=dlopen("/System/Library/PrivateFrameworks/IOKit.framework/IOKit",RTLD_NOW);
        if(h) {
            _create=(HIDCreate)dlsym(h,"IOHIDUserDeviceCreate"); _report=(HIDReport)dlsym(h,"IOHIDUserDeviceHandleReport");
            _copyService=(HIDService)dlsym(h,"IOHIDUserDeviceCopyService");
            _registryID=(RegistryID)dlsym(h,"IORegistryEntryGetRegistryEntryID");
            _objectRelease=(ObjectRelease)dlsym(h,"IOObjectRelease");
        }
        _error=@"native HID device has not been started";
    } return self;
}
- (BOOL)active { return _device!=NULL; }
- (NSString *)error { return _error; }
- (BOOL)submit:(const uint8_t *)report length:(CFIndex)length {
    if(!_device||!_report) { _error=@"native HID mode is disabled"; return NO; }
    int32_t result=_report(_device,report,length);
    if(result) { _error=[NSString stringWithFormat:@"IOHIDUserDevice report failed (0x%08x)",(unsigned)result]; return NO; }
    return YES;
}
- (BOOL)start {
    if(_device) return YES;
    if(!_create||!_report) { _error=@"IOHIDUserDevice APIs unavailable on this iOS version"; return NO; }
    NSDictionary *properties=@{@"ReportDescriptor":[NSData dataWithBytes:irb_hid_descriptor length:sizeof(irb_hid_descriptor)],
        @"Product":@"iOSRemoteBridge mouse and keyboard",@"Manufacturer":@"iOSRemoteBridge",
        @"Transport":@"Virtual",@"VendorID":@0x1209,@"ProductID":@0x4952,
        @"ReportInterval":@8000};
    _device=_create(kCFAllocatorDefault,(__bridge CFDictionaryRef)properties);
    if(!_device) { _error=@"IOHIDUserDeviceCreate failed; check daemon HID entitlement and jailbreak signing"; return NO; }
    memset(_keys,0,sizeof(_keys));
    uint8_t keyboard[9],mouse[7]; irb_hid_keyboard(_keys,keyboard); irb_hid_mouse(0,0,mouse);
    if(![self submit:keyboard length:sizeof(keyboard)]||![self submit:mouse length:sizeof(mouse)]) { [self stop]; return NO; }
    _error=nil; return YES;
}
- (BOOL)key:(uint16_t)usage down:(BOOL)down {
    if(usage<4||usage>231) return NO;
    uint8_t old=_keys[usage],report[9]; _keys[usage]=down;
    if(!irb_hid_keyboard(_keys,report)) { _keys[usage]=old; _error=@"native keyboard supports six simultaneous keys plus modifiers (US layout)"; return NO; }
    if(![self submit:report length:sizeof(report)]) { _keys[usage]=old; return NO; }
    ++_keyboardReports; return YES;
}
- (BOOL)mouseX:(double)x y:(double)y buttons:(uint8_t)buttons wheel:(int8_t)wheel {
    uint8_t position[5],mouse[7];
    if(buttons>31||!irb_hid_position(x,y,position)) return NO;
    // Absolute motion first, then relative button/wheel state at that position.
    irb_hid_mouse(buttons,wheel,mouse);
    if(![self submit:position length:sizeof(position)]||![self submit:mouse length:sizeof(mouse)]) return NO;
    ++_pointerReports; return YES;
}
- (NSDictionary *)diagnostics {
    uint64_t registry=0;
    if(_device&&_copyService&&_registryID&&_objectRelease) {
        uint32_t service=_copyService(_device);
        if(service) { _registryID(service,&registry); _objectRelease(service); }
    }
    return @{@"backend":@"IOHIDUserDevice",@"native_hid_active":IRBBool(self.active),
        @"registry_entry_id":@(registry),
        @"keyboard_reports":@(_keyboardReports),@"pointer_reports":@(_pointerReports)};
}
- (void)stop {
    if(!_device) return;
    memset(_keys,0,sizeof(_keys)); uint8_t keyboard[9],mouse[7];
    irb_hid_keyboard(_keys,keyboard); irb_hid_mouse(0,0,mouse);
    [self submit:keyboard length:sizeof(keyboard)]; [self submit:mouse length:sizeof(mouse)];
    CFRelease(_device); _device=NULL;
}
- (void)dealloc { [self stop]; }
@end
