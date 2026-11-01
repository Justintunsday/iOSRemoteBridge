#import "IRBEncoder.h"
#import "../common/IRBCommon.h"
#import <VideoToolbox/VideoToolbox.h>
#import <dlfcn.h>
@interface IRBEncoder ()
- (void)sample:(CMSampleBufferRef)sample status:(OSStatus)status start:(uint64_t)start;
@end
@implementation IRBEncoder {
    VTCompressionSessionRef _session;
    int _fps;
}
static void encoded(void *ref,void *source,OSStatus status,VTEncodeInfoFlags flags,CMSampleBufferRef sb) {
    (void)flags;
    [(__bridge IRBEncoder *)ref sample:sb status:status start:(uint64_t)(uintptr_t)source];
}
- (BOOL)property:(CFStringRef)key value:(id)value required:(BOOL)required {
    OSStatus s=VTSessionSetProperty(_session,key,(__bridge CFTypeRef)value);
    if(s&&required) _error=[NSString stringWithFormat:@"encoder property %@: %d",key,(int)s];
    return s==noErr||!required;
}
- (BOOL)startWidth:(int)w height:(int)h fps:(int)fps bitrate:(int)bitrate {
    [self stop]; _fps=fps;
    NSDictionary *spec=@{(id)kVTVideoEncoderSpecification_EnableLowLatencyRateControl:@YES};
    NSDictionary *attrs=@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),
                          (id)kCVPixelBufferIOSurfacePropertiesKey:@{}};
    OSStatus s=VTCompressionSessionCreate(NULL,w,h,kCMVideoCodecType_H264,(__bridge CFDictionaryRef)spec,
          (__bridge CFDictionaryRef)attrs,NULL,encoded,(__bridge void *)self,&_session);
    _lowLatency=s==noErr;
    if(s) { // Explicit compatibility fallback; telemetry reports lowLatency=false.
        s=VTCompressionSessionCreate(NULL,w,h,kCMVideoCodecType_H264,NULL,(__bridge CFDictionaryRef)attrs,
             NULL,encoded,(__bridge void *)self,&_session);
    }
    if(s) { _error=[NSString stringWithFormat:@"VTCompressionSessionCreate: %d",(int)s]; return NO; }
    BOOL ok=[self property:kVTCompressionPropertyKey_RealTime value:@YES required:YES]&&
       [self property:kVTCompressionPropertyKey_AllowFrameReordering value:@NO required:YES]&&
       [self property:kVTCompressionPropertyKey_ProfileLevel value:(id)kVTProfileLevel_H264_Main_AutoLevel required:YES]&&
       [self property:kVTCompressionPropertyKey_AverageBitRate value:@(bitrate) required:YES]&&
       [self property:kVTCompressionPropertyKey_ExpectedFrameRate value:@(fps) required:YES]&&
       [self property:kVTCompressionPropertyKey_MaxKeyFrameInterval value:@(fps*2) required:YES];
    [self property:kVTCompressionPropertyKey_MaxFrameDelayCount value:@1 required:NO];
    if(!ok||(s=VTCompressionSessionPrepareToEncodeFrames(_session))) {
        if(s) _error=[NSString stringWithFormat:@"prepare encoder: %d",(int)s]; [self stop]; return NO;
    }
    /* Do not link macOS-only/newer-iOS hardware requirement constants on 16.1.
       Read the runtime property where exposed, and report unknown otherwise. */
    CFStringRef *key=dlsym(RTLD_DEFAULT,"kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder");
    CFTypeRef value=NULL;
    _hardwareStatus=@"unknown (verify on device)";
    if(key&&VTSessionCopyProperty(_session,*key,NULL,&value)==noErr&&value) {
        if(CFGetTypeID(value)==CFBooleanGetTypeID()) _hardwareStatus=CFBooleanGetValue(value)?@"hardware":@"software";
        CFRelease(value);
    }
    if([_hardwareStatus isEqual:@"software"]) { _error=@"software encoder refused"; [self stop]; return NO; }
    return YES;
}
- (OSStatus)encode:(CVPixelBufferRef)buffer pts:(uint64_t)pts forceKeyframe:(BOOL)key {
    if(!_session) return kVTInvalidSessionErr;
    NSDictionary *props=key?@{(id)kVTEncodeFrameOptionKey_ForceKeyFrame:@YES}:nil;
    return VTCompressionSessionEncodeFrame(_session,buffer,CMTimeMake((int64_t)pts,1000000),CMTimeMake(1,_fps),
               (__bridge CFDictionaryRef)props,(void *)(uintptr_t)IRBNow(),NULL);
}
- (BOOL)setBitrate:(int)bitrate { return _session&&[self property:kVTCompressionPropertyKey_AverageBitRate value:@(bitrate) required:YES]; }
- (BOOL)setFPS:(int)fps {
    if(!_session||![self property:kVTCompressionPropertyKey_ExpectedFrameRate value:@(fps) required:YES]) return NO;
    _fps=fps; return YES;
}
- (void)sample:(CMSampleBufferRef)sb status:(OSStatus)status start:(uint64_t)start {
    if(status||!sb||!CMSampleBufferDataIsReady(sb)) { if(self.output) self.output(nil,NO,0,0,status?:-1); return; }
    BOOL key=YES;
    CFArrayRef a=CMSampleBufferGetSampleAttachmentsArray(sb,NO);
    if(a&&CFArrayGetCount(a)) key=!CFDictionaryContainsKey(CFArrayGetValueAtIndex(a,0),kCMSampleAttachmentKey_NotSync);
    NSMutableData *out=[NSMutableData data]; const uint8_t prefix[]={0,0,0,1};
    CMFormatDescriptionRef format=CMSampleBufferGetFormatDescription(sb);
    int nalLength=4;
    if(key) {
        size_t count=0,size=0; const uint8_t *p=NULL;
        OSStatus s=CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,0,&p,&size,&count,&nalLength);
        if(s) { if(self.output) self.output(nil,NO,0,0,s); return; }
        for(size_t i=0;i<count;++i) {
            if(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,i,&p,&size,NULL,NULL)) return;
            [out appendBytes:prefix length:4]; [out appendBytes:p length:size];
        }
    }
    if(nalLength!=4) { if(self.output) self.output(nil,NO,0,0,-2); return; }
    CMBlockBufferRef block=CMSampleBufferGetDataBuffer(sb);
    size_t size=block?CMBlockBufferGetDataLength(block):0;
    NSMutableData *avcc=[NSMutableData dataWithLength:size];
    if(!size||CMBlockBufferCopyDataBytes(block,0,size,avcc.mutableBytes)) { if(self.output) self.output(nil,NO,0,0,-3); return; }
    const uint8_t *p=avcc.bytes; size_t offset=0;
    while(offset+4<=size) {
        uint32_t n=irb_get32(p+offset); offset+=4;
        if(!n||n>size-offset) { if(self.output) self.output(nil,NO,0,0,-4); return; }
        [out appendBytes:prefix length:4]; [out appendBytes:p+offset length:n]; offset+=n;
    }
    if(offset!=size) { if(self.output) self.output(nil,NO,0,0,-5); return; }
    int64_t pts=CMTimeConvertScale(CMSampleBufferGetPresentationTimeStamp(sb),1000000,kCMTimeRoundingMethod_Default).value;
    if(self.output) self.output(out,key,(uint64_t)pts,(IRBNow()-start)/1000.0,noErr);
}
- (void)stop {
    if(_session) { VTCompressionSessionInvalidate(_session); CFRelease(_session); _session=NULL; }
}
- (void)dealloc { [self stop]; }
@end
