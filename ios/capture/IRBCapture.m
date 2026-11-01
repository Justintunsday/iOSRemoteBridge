// Capture approach informed by ScreenMirror (MIT), see THIRD_PARTY_NOTICES.md.
#import "IRBCapture.h"
#import <IOSurface/IOSurfaceRef.h>
#import <dlfcn.h>
#import <objc/message.h>
#import <mach/mach.h>
#import <CoreImage/CoreImage.h>
// Historical/independent callers declare a void return; do not fabricate a
// kern_return_t success value from the return register of this private API.
typedef void (*RenderDisplay)(mach_port_t,CFStringRef,IOSurfaceRef,int,int);
@implementation IRBCapture {
    RenderDisplay _render;
    CVPixelBufferPoolRef _pool;
    CVPixelBufferPoolRef _scaledPool;
    CIContext *_ci;
    int _nativeWidth,_nativeHeight;
    NSString *_displayName;
}
- (BOOL)prepareWithScale:(double)scale {
    if(!isfinite(scale)||scale<.25||scale>1) return NO;
    if(_pool) { CVPixelBufferPoolRelease(_pool); _pool=NULL; }
    if(_scaledPool) { CVPixelBufferPoolRelease(_scaledPool); _scaledPool=NULL; }
    void *lib=dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",RTLD_NOW);
    _render=lib?(RenderDisplay)dlsym(lib,"CARenderServerRenderDisplay"):NULL;
    if(!_render) { _error=@"CARenderServerRenderDisplay unavailable"; return NO; }
    _displayName=@"LCD";
    Class displayClass=NSClassFromString(@"CADisplay");
    SEL main=NSSelectorFromString(@"mainDisplay"),name=NSSelectorFromString(@"name");
    if([displayClass respondsToSelector:main]) {
        id display=((id(*)(id,SEL))objc_msgSend)(displayClass,main);
        if([display respondsToSelector:name]) {
            id resolved=((id(*)(id,SEL))objc_msgSend)(display,name);
            if([resolved isKindOfClass:NSString.class]&&[resolved length]) _displayName=resolved;
        }
    }
    CGSize s=UIScreen.mainScreen.nativeBounds.size;
    _nativeWidth=(int)s.width; _nativeHeight=(int)s.height;
    _width=MAX(2,((int)llround(s.width*scale))&~1);
    _height=MAX(2,((int)llround(s.height*scale))&~1);
    NSMutableDictionary *attrs=[@{(id)kCVPixelBufferWidthKey:@(_nativeWidth), (id)kCVPixelBufferHeightKey:@(_nativeHeight),
        (id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferIOSurfacePropertiesKey:@{}, (id)kCVPixelBufferBytesPerRowAlignmentKey:@64} mutableCopy];
    CVReturn r=CVPixelBufferPoolCreate(NULL,(__bridge CFDictionaryRef)@{(id)kCVPixelBufferPoolMinimumBufferCountKey:@3},
                                      (__bridge CFDictionaryRef)attrs,&_pool);
    if(r) _error=[NSString stringWithFormat:@"pixel pool: %d",r];
    if(!r&&(_width!=_nativeWidth||_height!=_nativeHeight)) {
        attrs[(id)kCVPixelBufferWidthKey]=@(_width); attrs[(id)kCVPixelBufferHeightKey]=@(_height);
        r=CVPixelBufferPoolCreate(NULL,NULL,(__bridge CFDictionaryRef)attrs,&_scaledPool);
        _ci=[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@NO,kCIContextCacheIntermediates:@NO}];
        if(r||!_ci) { _error=@"GPU scaling resources unavailable"; return NO; }
    }
    return r==kCVReturnSuccess;
}
- (CVPixelBufferRef)copyFrame {
    if(!_pool||!_render) return NULL;
    CVPixelBufferRef pb=NULL;
    NSDictionary *aux=@{(id)kCVPixelBufferPoolAllocationThresholdKey:@3};
    if(CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(NULL,_pool,(__bridge CFDictionaryRef)aux,&pb)) return NULL;
    IOSurfaceRef surface=CVPixelBufferGetIOSurface(pb);
    if(!surface) { _error=@"pixel buffer has no IOSurface"; CVPixelBufferRelease(pb); return NULL; }
    _render(MACH_PORT_NULL,(__bridge CFStringRef)_displayName,surface,0,0);
    /* No screenshot fallback, black-frame substitution, secure-layer flags or
       protected-content entitlements. A symbol resolving does not verify pixels. */
    if(!_scaledPool) return pb;
    CVPixelBufferRef scaled=NULL;
    if(CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(NULL,_scaledPool,(__bridge CFDictionaryRef)aux,&scaled)) {
        CVPixelBufferRelease(pb); return NULL;
    }
    CIImage *image=[[CIImage imageWithCVPixelBuffer:pb] imageByApplyingTransform:
        CGAffineTransformMakeScale((CGFloat)_width/_nativeWidth,(CGFloat)_height/_nativeHeight)];
    [_ci render:image toCVPixelBuffer:scaled];
    CVPixelBufferRelease(pb); return scaled;
}
- (void)dealloc { if(_pool) CVPixelBufferPoolRelease(_pool); if(_scaledPool) CVPixelBufferPoolRelease(_scaledPool); }
@end
