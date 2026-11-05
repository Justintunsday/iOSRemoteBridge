#import "../ios/common/IRBValidation.h"
#include <stdio.h>
#include <stdlib.h>
static void require(BOOL value) { if(!value) { fprintf(stderr,"command validation failed\n"); exit(1); } }
int main(void) { @autoreleasepool {
    require(IRBValidateRequest(@{@"op":@"tap",@"id":@1,@"x":@.5,@"y":@.3},NO));
    require(IRBValidateRequest(@{@"op":@"control_center",@"id":@1},NO));
    require(IRBValidateRequest(@{@"op":@"insert_text",@"id":@1,@"text":@"AI 直接输入\n你好 😀 👩🏽‍💻 café"},NO));
    require(IRBValidateRequest(@{@"op":@"insert_text",@"id":@1,@"text":@""},NO));
    NSString *boundary=[@"😀" stringByPaddingToLength:1024 withString:@"😀" startingAtIndex:0];
    require(IRBValidateRequest(@{@"op":@"insert_text",@"id":@1,@"text":boundary},NO));
    require(!IRBValidateRequest(@{@"op":@"insert_text",@"id":@1,@"text":[boundary stringByAppendingString:@"x"]},NO));
    unichar unpaired[]={0xd800};
    require(!IRBValidLiteralText([NSString stringWithCharacters:unpaired length:1]));
    unpaired[0]=0xdc00;
    require(!IRBValidLiteralText([NSString stringWithCharacters:unpaired length:1]));
    require(IRBValidateRequest(@{@"op":@"keyboard_mode",@"id":@1,@"enabled":@YES},NO));
    require(IRBValidateRequest(@{@"op":@"key_event",@"id":@1,@"usage":@225,@"down":@NO},NO));
    require(IRBValidateRequest(@{@"op":@"mouse_event",@"id":@1,@"x":@0,@"y":@1,@"buttons":@31,@"wheel":@-127},NO));
    require(IRBValidateRequest(@{@"op":@"mouse_delta",@"id":@1,@"dx":@-32767,@"dy":@32767,@"buttons":@31,@"wheel":@-127},NO));
    require(IRBValidateRequest(@{@"op":@"configure",@"id":@0,@"bitrate":@1000000},YES));
    require(!IRBValidateRequest(@{@"op":@"configure",@"id":@0,@"bitrate":@1000000},NO));
    NSArray *bad=@[
        @{@"op":@"tap",@"id":@1,@"x":NSNull.null,@"y":@.3},
        @{@"op":@"tap",@"id":@1,@"x":@[],@"y":@.3},
        @{@"op":@"tap",@"id":@1,@"x":@".5",@"y":@.3},
        @{@"op":@"tap",@"id":@1,@"x":@(NAN),@"y":@.3},
        @{@"op":@"touch",@"id":@1,@"x":@.5,@"y":@.3,@"finger":@.1,@"phase":@0},
        @{@"op":@"touch",@"id":@1,@"x":@.5,@"y":@.3,@"finger":@10,@"phase":@0},
        @{@"op":@"type_text",@"id":@1,@"text":@{}},
        @{@"op":@"insert_text",@"id":@1,@"text":@{}},
        @{@"op":@"insert_text",@"id":@1},
        @{@"op":@"insert_text",@"id":@1,@"text":@"ok",@"usage":@40},
        @{@"op":@"get_device_info",@"id":@YES},
        @{@"op":@"get_device_info",@"id":@1,@"extra":@"unknown"},
        @{@"op":@"configure",@"id":@1,@"bitrate":@1000000,@"fps":@60},
        @{@"op":@"configure",@"id":@1,@"bitrate":@1000000,@"fps":@45,@"scale":@.5},
        @{@"op":@"launch_app",@"id":@1,@"bundle_id":NSNull.null},
        @{@"op":@"key",@"id":@1,@"usage":@65540},
        @{@"op":@"key",@"id":@1,@"usage":@40,@"down":@YES},
        @{@"op":@"keyboard_mode",@"id":@1,@"enabled":@1},
        @{@"op":@"key_event",@"id":@1,@"usage":@225,@"down":@"false"},
        @{@"op":@"key_event",@"id":@1,@"usage":@232,@"down":@YES},
        @{@"op":@"mouse_event",@"id":@1,@"x":@.5,@"y":@.5,@"buttons":@32,@"wheel":@0},
        @{@"op":@"mouse_event",@"id":@1,@"x":@.5,@"y":@.5,@"buttons":@YES,@"wheel":@0},
        @{@"op":@"mouse_event",@"id":@1,@"x":@.5,@"y":@.5,@"buttons":@0,@"wheel":@128},
        @{@"op":@"mouse_event",@"id":@1,@"x":@.5,@"y":@.5,@"buttons":@0,@"wheel":@.5},
        @{@"op":@"mouse_event",@"id":@1,@"x":@.5,@"y":@.5,@"buttons":@0},
        @{@"op":@"mouse_delta",@"id":@1,@"dx":@32768,@"dy":@0,@"buttons":@0,@"wheel":@0},
        @{@"op":@"mouse_delta",@"id":@1,@"dx":@YES,@"dy":@0,@"buttons":@0,@"wheel":@0},
        @{@"op":@"mouse_delta",@"id":@1,@"dx":@.5,@"dy":@0,@"buttons":@0,@"wheel":@0},
        @{@"op":@"mouse_delta",@"id":@1,@"dx":@0,@"dy":NSNull.null,@"buttons":@0,@"wheel":@0},
        @{@"op":@"unknown",@"id":@1}
    ];
    for(NSDictionary *r in bad) require(!IRBValidateRequest(r,NO));
    require(IRBDictionary(nil)==nil);
    require(IRBDictionary([@"[]" dataUsingEncoding:NSUTF8StringEncoding])==nil);
    for(NSString *error in @[@"",@"simulated rejection"]) {
        NSDictionary *reply=IRBDictionary(IRBJSON(IRBReply(@{@"id":@42},@{@"state":@"submitted"},error.length?error:nil)));
        require(CFGetTypeID((__bridge CFTypeRef)reply[@"ok"])==CFBooleanGetTypeID());
        require([reply[@"ok"] boolValue]==(error.length==0));
        require([reply[@"id"] isEqual:@42]);
        require(error.length?reply[@"error"]!=nil:reply[@"result"]!=nil);
    }
    puts("Foundation schema checks and real JSON boolean reply serialization passed");
} return 0; }
