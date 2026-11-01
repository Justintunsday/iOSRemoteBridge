#import "IRBNativeHID.h"
#import "../common/IRBCommon.h"
#import <dlfcn.h>
#import <mach/mach.h>
#include "../../shared/irb_hid_reports.h"
typedef CFTypeRef HIDDevice;
typedef HIDDevice (*HIDCreate)(CFAllocatorRef,CFDictionaryRef);
typedef int32_t (*HIDReport)(HIDDevice,const uint8_t *,CFIndex);
typedef uint32_t (*HIDService)(HIDDevice);
typedef int32_t (*RegistryID)(uint32_t,uint64_t *);
typedef int32_t (*ObjectRelease)(uint32_t);
@implementation IRBNativeHID {
    HIDDevice _devices[IRB_HID_DEVICE_COUNT];
    HIDCreate _create;
    HIDReport _report;
    HIDService _copyService;
    RegistryID _registryID;
    ObjectRelease _objectRelease;
    uint8_t _keys[256];
    uint64_t _keyboardReports,_pointerReports;
    uint64_t _relativeReports;
    NSString *_error;
    void *_iokit;
    NSDictionary *_creationProbe;
}
- (instancetype)init {
    if((self=[super init])) {
        void *h=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW);
        if(!h) h=dlopen("/System/Library/PrivateFrameworks/IOKit.framework/IOKit",RTLD_NOW);
        if(h) {
            _iokit=h;
            _create=(HIDCreate)dlsym(h,"IOHIDUserDeviceCreate"); _report=(HIDReport)dlsym(h,"IOHIDUserDeviceHandleReport");
            _copyService=(HIDService)dlsym(h,"IOHIDUserDeviceCopyService");
            _registryID=(RegistryID)dlsym(h,"IORegistryEntryGetRegistryEntryID");
            _objectRelease=(ObjectRelease)dlsym(h,"IOObjectRelease");
        }
        _error=@"native HID device has not been started";
    } return self;
}
- (BOOL)active {
    for(unsigned i=0;i<IRB_HID_DEVICE_COUNT;++i) if(!_devices[i]) return NO;
    return YES;
}
- (NSString *)error { return _error; }
- (void)probeCreationFailure {
    NSMutableDictionary *probe=[NSMutableDictionary new];
    CFMutableDictionaryRef (*matching)(const char *)=dlsym(_iokit,"IOServiceMatching");
    uint32_t (*find)(uint32_t,CFDictionaryRef)=dlsym(_iokit,"IOServiceGetMatchingService");
    int32_t (*openService)(uint32_t,uint32_t,uint32_t,uint32_t *)=dlsym(_iokit,"IOServiceOpen");
    int32_t (*closeService)(uint32_t)=dlsym(_iokit,"IOServiceClose");
    if(matching&&find&&openService&&closeService&&_objectRelease) {
        uint32_t service=find(0,matching("IOHIDResource")),connection=0;
        probe[@"resource_found"]=IRBBool(service!=0);
        if(service) {
            int32_t code=openService(service,mach_task_self(),0,&connection);
            probe[@"open_result"]=[NSString stringWithFormat:@"0x%08x",(unsigned)code];
            if(connection) closeService(connection); _objectRelease(service);
        }
    }
    void *security=dlopen("/System/Library/Frameworks/Security.framework/Security",RTLD_NOW);
    CFTypeRef (*taskCreate)(CFAllocatorRef)=security?dlsym(security,"SecTaskCreateFromSelf"):NULL;
    CFTypeRef (*entitlement)(CFTypeRef,CFStringRef,CFErrorRef *)=security?dlsym(security,"SecTaskCopyValueForEntitlement"):NULL;
    if(taskCreate&&entitlement) {
        CFTypeRef task=taskCreate(NULL);
        if(task) {
            CFTypeRef value=entitlement(task,CFSTR("com.apple.hid.manager.user-access-device"),NULL);
            probe[@"runtime_hid_entitlement"]=IRBBool(value&&CFEqual(value,kCFBooleanTrue));
            if(value) CFRelease(value); CFRelease(task);
        }
    }
    _creationProbe=probe;
}
- (BOOL)submit:(const uint8_t *)report length:(CFIndex)length {
    int device=length>0?irb_hid_report_device(report[0]):-1;
    if(device<0||!_devices[device]||!_report) { _error=@"native HID mode is disabled"; return NO; }
    int32_t result=_report(_devices[device],report,length);
    if(result) { _error=[NSString stringWithFormat:@"IOHIDUserDevice report failed (0x%08x)",(unsigned)result]; return NO; }
    return YES;
}
- (BOOL)start {
    if(self.active) return YES;
    [self stop];
    if(!_create||!_report) { _error=@"IOHIDUserDevice APIs unavailable on this iOS version"; return NO; }
    NSArray *products=@[@"iOSRemoteBridge keyboard",@"iOSRemoteBridge native mouse"];
    for(unsigned i=0;i<IRB_HID_DEVICE_COUNT;++i) {
        size_t length=0; const uint8_t *descriptor=irb_hid_device_descriptor(i,&length);
        NSDictionary *properties=@{@"ReportDescriptor":[NSData dataWithBytes:descriptor length:length],
            @"Product":products[i],@"Manufacturer":@"iOSRemoteBridge",@"Transport":@"Virtual",
            @"VendorID":@0x1209,@"ProductID":@(i==IRB_HID_KEYBOARD?0x4952:0x4954),@"ReportInterval":@8000};
        _devices[i]=_create(kCFAllocatorDefault,(__bridge CFDictionaryRef)properties);
        if(!_devices[i]) {
            [self probeCreationFailure]; [self stop];
            _error=[NSString stringWithFormat:@"Native HID device %u creation failed (%@)",i,_creationProbe]; return NO;
        }
    }
    memset(_keys,0,sizeof(_keys));
    uint8_t keyboard[9],mouse[7]; irb_hid_keyboard(_keys,keyboard); irb_hid_mouse(0,0,0,0,mouse);
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
- (BOOL)mouseDX:(int16_t)dx dy:(int16_t)dy buttons:(uint8_t)buttons wheel:(int8_t)wheel {
    if(buttons>31||dx==INT16_MIN||dy==INT16_MIN) return NO;
    uint8_t mouse[7]; irb_hid_mouse(dx,dy,buttons,wheel,mouse);
    if(![self submit:mouse length:sizeof(mouse)]) return NO;
    ++_pointerReports; ++_relativeReports; return YES;
}
- (NSDictionary *)diagnostics {
    NSMutableArray *ids=[NSMutableArray new];
    for(unsigned i=0;i<IRB_HID_DEVICE_COUNT;++i) {
        uint64_t registry=0;
        if(_devices[i]&&_copyService&&_registryID&&_objectRelease) {
            uint32_t service=_copyService(_devices[i]);
            if(service) {
                _registryID(service,&registry);
                _objectRelease(service);
            }
        }
        [ids addObject:@(registry)];
    }
    return @{@"backend":@"IOHIDUserDevice",@"native_hid_active":IRBBool(self.active),
        @"registry_entry_id":ids[IRB_HID_MOUSE],@"registry_entry_ids":ids,
        @"pointer_mode":@"relative",@"relative_reports":@(_relativeReports),
        @"creation_probe":_creationProbe?:@{},
        @"keyboard_reports":@(_keyboardReports),@"pointer_reports":@(_pointerReports)};
}
- (void)stop {
    NSString *savedError=_error;
    memset(_keys,0,sizeof(_keys)); uint8_t keyboard[9],mouse[7];
    irb_hid_keyboard(_keys,keyboard); irb_hid_mouse(0,0,0,0,mouse);
    if(_devices[IRB_HID_KEYBOARD]) [self submit:keyboard length:sizeof(keyboard)];
    if(_devices[IRB_HID_MOUSE]) [self submit:mouse length:sizeof(mouse)];
    for(unsigned i=0;i<IRB_HID_DEVICE_COUNT;++i) if(_devices[i]) { CFRelease(_devices[i]); _devices[i]=NULL; }
    _error=savedError;
}
- (void)dealloc { [self stop]; }
@end
