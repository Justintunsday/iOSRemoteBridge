#import "../ios/common/IRBValidation.h"
#include <stdio.h>
#include <stdlib.h>
static void require(BOOL value) { if(!value) { fprintf(stderr,"command validation failed\n"); exit(1); } }
int main(void) { @autoreleasepool {
    require(IRBValidateRequest(@{@"op":@"tap",@"id":@1,@"x":@.5,@"y":@.3},NO));
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
        @{@"op":@"get_device_info",@"id":@YES},
        @{@"op":@"get_device_info",@"id":@1,@"extra":@"unknown"},
        @{@"op":@"configure",@"id":@1,@"bitrate":@1000000,@"fps":@60},
        @{@"op":@"configure",@"id":@1,@"bitrate":@1000000,@"fps":@45,@"scale":@.5},
        @{@"op":@"launch_app",@"id":@1,@"bundle_id":NSNull.null},
        @{@"op":@"key",@"id":@1,@"usage":@65540},
        @{@"op":@"key",@"id":@1,@"usage":@40,@"down":@YES},
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
