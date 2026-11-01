// Capture approach informed by ScreenMirror (MIT), see THIRD_PARTY_NOTICES.md.
#import "IRBCapture.h"
#import <IOSurface/IOSurface.h>
#import <dlfcn.h>
#import <mach/mach.h>
typedef kern_return_t (*RenderDisplay)(mach_port_t,CFStringRef,IOSurfaceRef,int,int);
@implementation IRBCapture {
    RenderDisplay _render;
    CVPixelBufferPoolRef _pool;
}
- (BOOL)prepareWithScale:(double)scale {
    if(!isfinite(scale)||scale<.25||scale>1) return NO;
    if(_pool) { CVPixelBufferPoolRelease(_pool); _pool=NULL; }
    void *lib=dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",RTLD_NOW);
    _render=lib?(RenderDisplay)dlsym(lib,"CARenderServerRenderDisplay"):NULL;
    if(!_render) { _error=@"CARenderServerRenderDisplay unavailable"; return NO; }
    CGSize s=UIScreen.mainScreen.nativeBounds.size;
    _width=MAX(2,((int)llround(s.width*scale))&~1);
    _height=MAX(2,((int)llround(s.height*scale))&~1);
    NSDictionary *attrs=@{(id)kCVPixelBufferWidthKey:@(_width), (id)kCVPixelBufferHeightKey:@(_height),
        (id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferIOSurfacePropertiesKey:@{}, (id)kCVPixelBufferBytesPerRowAlignmentKey:@64};
    CVReturn r=CVPixelBufferPoolCreate(NULL,(__bridge CFDictionaryRef)@{(id)kCVPixelBufferPoolMinimumBufferCountKey:@3},
                                      (__bridge CFDictionaryRef)attrs,&_pool);
    if(r) _error=[NSString stringWithFormat:@"pixel pool: %d",r];
    return r==kCVReturnSuccess;
}
- (CVPixelBufferRef)copyFrame {
    if(!_pool||!_render) return NULL;
    CVPixelBufferRef pb=NULL;
    NSDictionary *aux=@{(id)kCVPixelBufferPoolAllocationThresholdKey:@3};
    if(CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(NULL,_pool,(__bridge CFDictionaryRef)aux,&pb)) return NULL;
    IOSurfaceRef surface=CVPixelBufferGetIOSurface(pb);
    kern_return_t r=surface?_render(MACH_PORT_NULL,NULL,surface,0,0):KERN_FAILURE;
    if(r!=KERN_SUCCESS) { _error=[NSString stringWithFormat:@"render returned %d",r]; CVPixelBufferRelease(pb); return NULL; }
    /* No screenshot fallback, black-frame substitution, secure-layer flags or
       protected-content entitlements. A symbol resolving does not verify pixels. */
    return pb;
}
- (void)dealloc { if(_pool) CVPixelBufferPoolRelease(_pool); }
@end
