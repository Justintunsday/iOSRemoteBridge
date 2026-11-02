#import "IRBScreenState.h"
#import <objc/message.h>
#include <string.h>
static id sharedController(NSString *name) {
    Class cls=NSClassFromString(name);SEL shared=NSSelectorFromString(@"sharedInstance");
    return [cls respondsToSelector:shared]?((id(*)(id,SEL))objc_msgSend)(cls,shared):nil;
}
@implementation IRBScreenState
+ (BOOL)locked {
    id manager=sharedController(@"SBLockScreenManager");SEL selector=NSSelectorFromString(@"isUILocked");
    return [manager respondsToSelector:selector]?((BOOL(*)(id,SEL))objc_msgSend)(manager,selector):YES;
}
+ (BOOL)screenKnown { return [sharedController(@"SBBacklightController") respondsToSelector:NSSelectorFromString(@"screenIsOn")]; }
+ (BOOL)screenOn {
    id controller=sharedController(@"SBBacklightController");SEL selector=NSSelectorFromString(@"screenIsOn");
    return [controller respondsToSelector:selector]?((BOOL(*)(id,SEL))objc_msgSend)(controller,selector):YES;
}
+ (NSMethodSignature *)wakeSignature:(id)controller {
    SEL selector=NSSelectorFromString(@"turnOnScreenFullyWithBacklightSource:");
    if(![controller respondsToSelector:selector]) return nil;
    NSMethodSignature *signature=[controller methodSignatureForSelector:selector];
    if(signature.numberOfArguments!=3||strcmp(signature.methodReturnType,@encode(void))) return nil;
    const char *type=[signature getArgumentTypeAtIndex:2];
    return !strcmp(type,@encode(long long))||!strcmp(type,@encode(unsigned long long))||!strcmp(type,@encode(int))||!strcmp(type,@encode(unsigned int))?signature:nil;
}
+ (BOOL)wakeAvailable { return [self wakeSignature:sharedController(@"SBBacklightController")]!=nil; }
+ (BOOL)wake {
    // Set the backlight on, never toggle power or call an unlock/passcode API.
    id controller=sharedController(@"SBBacklightController");NSMethodSignature *signature=[self wakeSignature:controller];
    if(!signature) return NO;
    @try {
        NSInvocation *invocation=[NSInvocation invocationWithMethodSignature:signature];
        invocation.target=controller;invocation.selector=NSSelectorFromString(@"turnOnScreenFullyWithBacklightSource:");
        long long source=2;[invocation setArgument:&source atIndex:2];[invocation invoke];return YES;
    } @catch(NSException *exception) { (void)exception;return NO; }
}
@end
