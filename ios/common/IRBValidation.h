#import "IRBCommon.h"
#import <CoreFoundation/CoreFoundation.h>
static inline BOOL IRBFiniteNumber(id value,double minimum,double maximum,BOOL integer) {
    if(!IRBNumber(value)||CFGetTypeID((__bridge CFTypeRef)value)==CFBooleanGetTypeID()) return NO;
    double n=[value doubleValue];
    return isfinite(n)&&n>=minimum&&n<=maximum&&(!integer||floor(n)==n);
}
/* Validate again in SpringBoard, even after daemon validation. No untrusted
   object may receive numeric/string selectors before its type is checked. */
static inline BOOL IRBValidateRequest(NSDictionary *r,BOOL internal) {
    if(![r isKindOfClass:NSDictionary.class]||![r[@"op"] isKindOfClass:NSString.class]||
       !IRBFiniteNumber(r[@"id"],internal?0:1,2147483647,YES)) return NO;
    static NSDictionary<NSString *,NSArray<NSString *> *> *schemas;
    static dispatch_once_t once;
    dispatch_once(&once,^{ schemas=@{
        @"get_device_info":@[],@"get_screen_frame":@[],@"get_orientation":@[],
        @"get_connection_status":@[],@"get_performance_stats":@[],@"request_keyframe":@[],
        @"disconnect":@[],@"press_home":@[],@"app_switcher":@[],@"control_center":@[],@"volume_up":@[],@"volume_down":@[],
        @"keyboard_mode":@[@"enabled"],@"key_event":@[@"usage",@"down"],
        @"tap":@[@"x",@"y"],@"double_tap":@[@"x",@"y"],@"long_press":@[@"x",@"y",@"duration_ms"],
        @"swipe":@[@"x",@"y",@"to_x",@"to_y",@"duration_ms"],
        @"touch":@[@"x",@"y",@"finger",@"phase"],@"type_text":@[@"text"],
        @"key":@[@"usage"],@"launch_app":@[@"bundle_id"],@"configure":@[@"bitrate",@"fps",@"scale"]
    }; });
    NSString *op=r[@"op"]; NSArray *fields=schemas[op]; if(!fields) return NO;
    for(id key in r) if(![key isEqual:@"id"]&&![key isEqual:@"op"]&&![fields containsObject:key]) return NO;
    for(NSString *key in fields) {
        if([op isEqual:@"configure"]&&([key isEqual:@"fps"]||[key isEqual:@"scale"])) continue;
        if(!r[key]) return NO;
    }
    for(NSString *key in @[@"x",@"y",@"to_x",@"to_y"]) if(r[key]&&!IRBFiniteNumber(r[key],0,1,NO)) return NO;
    if(r[@"finger"]&&!IRBFiniteNumber(r[@"finger"],0,9,YES)) return NO;
    if(r[@"phase"]&&!IRBFiniteNumber(r[@"phase"],0,2,YES)) return NO;
    if(r[@"duration_ms"]&&!IRBFiniteNumber(r[@"duration_ms"],40,5000,YES)) return NO;
    if(r[@"usage"]&&!IRBFiniteNumber(r[@"usage"],4,231,YES)) return NO;
    for(NSString *key in @[@"enabled",@"down"]) if(r[key]&&
        (!IRBNumber(r[key])||CFGetTypeID((__bridge CFTypeRef)r[key])!=CFBooleanGetTypeID())) return NO;
    if(r[@"bitrate"]&&!IRBFiniteNumber(r[@"bitrate"],500000,20000000,YES)) return NO;
    if(r[@"fps"]||r[@"scale"]) {
        if(!IRBFiniteNumber(r[@"fps"],30,60,YES)||([r[@"fps"] intValue]!=30&&[r[@"fps"] intValue]!=60)||
           !IRBFiniteNumber(r[@"scale"],.25,1,NO)) return NO;
    }
    if(r[@"text"]&&(![r[@"text"] isKindOfClass:NSString.class]||[r[@"text"] length]>1024)) return NO;
    if(r[@"bundle_id"]&&(![r[@"bundle_id"] isKindOfClass:NSString.class]||[r[@"bundle_id"] length]<1||[r[@"bundle_id"] length]>255)) return NO;
    return YES;
}
