#import "IRBSimulatorSmoke.h"
#import "IRBControllerViewController.h"
#import <TargetConditionals.h>
#if TARGET_OS_SIMULATOR
static BOOL smokeRequested(void) { return [NSProcessInfo.processInfo.arguments containsObject:@"--irb-smoke"]; }
static NSURL *smokeFile(NSString *name) { return [[NSFileManager.defaultManager URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:name]; }
static NSError *keychainError;
void IRBPrepareSimulatorSmoke(void) {
    if(!smokeRequested()) return;
    NSData *data=[NSData dataWithContentsOfURL:smokeFile(@"irb-smoke-profile.json")];
    IRBProfile *profile=data?[IRBProfile fromData:data error:NULL]:nil;
    NSError *error=nil;if(profile) [IRBProfileStore save:profile error:&error];keychainError=error;
}
void IRBStartSimulatorSmoke(IRBControllerViewController *controller) {
    if([NSProcessInfo.processInfo.arguments containsObject:@"--irb-warmup"]) {
        [@"ready" writeToURL:smokeFile(@"irb-warmup-ready") atomically:YES encoding:NSUTF8StringEncoding error:NULL];return;
    }
    if(!smokeRequested()) return;
    NSData *data=[NSData dataWithContentsOfURL:smokeFile(@"irb-smoke-profile.json")];IRBProfile *profile=data?[IRBProfile fromData:data error:NULL]:nil;
    NSData *optionsData=[NSData dataWithContentsOfURL:smokeFile(@"irb-smoke-options.json")];
    NSDictionary *options=optionsData?[NSJSONSerialization JSONObjectWithData:optionsData options:0 error:NULL]:@{};
    BOOL pinFailure=[options[@"pin_failure"] boolValue];
    NSMutableDictionary *result=[@{@"passed":@NO,@"decoded_frames":@0,@"responses":@0,@"pin_failure":@(pinFailure)} mutableCopy];
    __block BOOL finished=NO,sent=NO;
    void(^finish)(BOOL,NSString *)=^(BOOL ok,NSString *reason) {
        if(finished) return;finished=YES;result[@"passed"]=ok?@YES:@NO;result[@"reason"]=reason;
        [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL] writeToURL:smokeFile(@"irb-smoke-result.json") atomically:YES];
    };
    void(^maybeFinish)(void)=^{if([result[@"decoded_frames"] intValue]>=3&&[result[@"responses"] intValue]>=5) finish([result[@"pixels_valid"] boolValue]&&[result[@"coordinates_valid"] boolValue],@"video, color, coordinate mapping and ordered control replies");};
    void(^oldState)(IRBSessionState,NSString *)=controller.session.onState;
    controller.session.onState=^(IRBSessionState state,NSString *message) {
        if(oldState) oldState(state,message);
        if(state==IRBSessionIdle&&[message containsString:@"证书"]) finish(pinFailure,message);
    };
    void(^oldFrame)(CVPixelBufferRef,uint64_t)=controller.session.onFrame;
    controller.session.onFrame=^(CVPixelBufferRef image,uint64_t pts) {
        if(oldFrame) oldFrame(image,pts);result[@"decoded_frames"]=@([result[@"decoded_frames"] intValue]+1);
        if(!sent) {
            sent=YES;size_t width=CVPixelBufferGetWidth(image),height=CVPixelBufferGetHeight(image);CVPixelBufferLockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
            uint8_t *plane=CVPixelBufferGetBaseAddressOfPlane(image,0);int black=plane?plane[0]:-1,white=plane?plane[width-1]:-1;
            CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
            result[@"width"]=@(width);result[@"height"]=@(height);result[@"black"]=@(black);result[@"white"]=@(white);
            result[@"pixels_valid"]=@(width==256&&height==16&&black>=0&&black<=8&&white>=240);
            CGPoint natural;CGRect bounds=controller.videoView.bounds;
            BOOL center=[controller.videoView normalizedPoint:CGPointMake(CGRectGetMidX(bounds),CGRectGetMidY(bounds)) clamp:NO result:&natural]&&fabs(natural.x-.5)<.001&&fabs(natural.y-.5)<.001;
            BOOL letterbox=![controller.videoView normalizedPoint:CGPointMake(1,1) clamp:NO result:&natural];result[@"coordinates_valid"]=@(center&&letterbox);
            for(NSDictionary *request in @[@{@"op":@"touch",@"finger":@0,@"phase":@0,@"x":@.25,@"y":@.75},@{@"op":@"touch",@"finger":@0,@"phase":@1,@"x":@.4,@"y":@.5},@{@"op":@"touch",@"finger":@0,@"phase":@2,@"x":@.4,@"y":@.5},@{@"op":@"press_home"},@{@"op":@"type_text",@"text":@"IRB"}]) [controller.session command:request];
        }
        maybeFinish();
    };
    controller.session.onResponse=^(NSDictionary *reply) {
        if([reply[@"id"] isKindOfClass:NSNumber.class]) {
            if(![reply[@"ok"] boolValue]) {finish(NO,@"control request failed");return;}
            result[@"responses"]=@([result[@"responses"] intValue]+1);maybeFinish();
        }
    };
    if(!profile) {finish(NO,@"test profile missing");return;}
    IRBProfile *stored=IRBProfileStore.load;
    result[@"keychain_valid"]=@([stored.host isEqual:profile.host]&&stored.port==profile.port&&[stored.token isEqual:profile.token]&&[stored.certificatePin isEqual:profile.certificatePin]);
    if(![result[@"keychain_valid"] boolValue]) {result[@"keychain_status"]=@(keychainError.code);finish(NO,@"pairing Keychain round trip failed");return;}
    [controller.session connect:profile fps:60 control:YES];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,35*NSEC_PER_SEC),dispatch_get_main_queue(),^{finish(NO,@"simulator integration timed out");});
}
#else
void IRBPrepareSimulatorSmoke(void) {}
void IRBStartSimulatorSmoke(IRBControllerViewController *controller) { (void)controller; }
#endif
