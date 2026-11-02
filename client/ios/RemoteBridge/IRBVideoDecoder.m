#import "IRBVideoDecoder.h"
#import <VideoToolbox/VideoToolbox.h>
#import <CoreMedia/CoreMedia.h>
static NSUInteger prefix(const uint8_t *p,NSUInteger n,NSUInteger at) {
    if(at+4<=n&&!p[at]&&!p[at+1]&&!p[at+2]&&p[at+3]==1) return 4;
    if(at+3<=n&&!p[at]&&!p[at+1]&&p[at+2]==1) return 3;return 0;
}
NSArray<NSData *> *IRBSplitAnnexB(NSData *data) {
    const uint8_t *p=data.bytes;NSUInteger start=NSNotFound;NSMutableArray *nals=[NSMutableArray new];
    for(NSUInteger at=0;at<data.length;) {
        NSUInteger length=prefix(p,data.length,at);
        if(!length) { ++at;continue; }
        if(start!=NSNotFound&&at>start) [nals addObject:[data subdataWithRange:NSMakeRange(start,at-start)]];
        start=at+length;at=start;
    }
    if(start!=NSNotFound&&start<data.length) [nals addObject:[data subdataWithRange:NSMakeRange(start,data.length-start)]];
    return nals;
}
@implementation IRBVideoDecoder {
    dispatch_queue_t _queue;
    VTDecompressionSessionRef _session;
    CMVideoFormatDescriptionRef _format;
    NSData *_sps,*_pps;
    BOOL _needKey;
}
- (instancetype)init { if((self=[super init])) { _queue=dispatch_queue_create("irb.controller.decode",dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_USER_INTERACTIVE,0));_needKey=YES; }return self; }
- (void)resetSession {
    if(_session) { VTDecompressionSessionInvalidate(_session);CFRelease(_session);_session=NULL; }
    if(_format) { CFRelease(_format);_format=NULL; }
    _needKey=YES;
}
static void decoded(void *context,void *frame,OSStatus status,VTDecodeInfoFlags flags,CVImageBufferRef image,CMTime pts,CMTime duration) {
    (void)context;(void)flags;(void)pts;(void)duration;
    // DecodeFrame is synchronous (flags=0) on the dedicated decoder queue.
    // Exactly one access unit is supplied, so this stack reference stays valid.
    if(!status&&image&&frame) { CVPixelBufferRef *output=frame;if(*output) CVPixelBufferRelease(*output);*output=CVPixelBufferRetain(image); }
}
- (BOOL)prepare {
    if(!_sps.length||!_pps.length) return NO;
    const uint8_t *sets[]={_sps.bytes,_pps.bytes};size_t lengths[]={_sps.length,_pps.length};
    if(CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL,2,sets,lengths,4,&_format)) return NO;
    VTDecompressionOutputCallbackRecord callback={decoded,NULL};
    NSDictionary *attributes=@{(__bridge id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),(__bridge id)kCVPixelBufferIOSurfacePropertiesKey:@{}};
    if(VTDecompressionSessionCreate(NULL,_format,NULL,(__bridge CFDictionaryRef)attributes,&callback,&_session)) { CFRelease(_format);_format=NULL;return NO; }
    VTSessionSetProperty(_session,kVTDecompressionPropertyKey_RealTime,kCFBooleanTrue);return YES;
}
- (void)decode:(NSData *)data keyframe:(BOOL)key completion:(void(^)(CVPixelBufferRef,NSString *))completion {
    dispatch_async(_queue,^{ @autoreleasepool {
        NSArray<NSData *> *nals=IRBSplitAnnexB(data);NSMutableData *avcc=[NSMutableData new];BOOL changed=NO,hasPicture=NO;
        for(NSData *nal in nals) {
            if(!nal.length) continue;uint8_t type=((const uint8_t *)nal.bytes)[0]&31;
            if(type==7||type==8) {
                if(nal.length>65536) { completion(NULL,@"视频参数过大");return; }
                NSData *old=type==7?self->_sps:self->_pps;
                if(![old isEqualToData:nal]) { changed=YES;if(type==7) self->_sps=nal;else self->_pps=nal; }continue;
            }
            if(type==9) continue;
            if(type==1||type==5) hasPicture=YES;
            uint32_t length=CFSwapInt32HostToBig((uint32_t)nal.length);[avcc appendBytes:&length length:4];[avcc appendData:nal];
        }
        if(changed) [self resetSession];
        if(!hasPicture||(self->_needKey&&!key)) { completion(NULL,@"等待视频关键帧");return; }
        if(!self->_session&&![self prepare]) { completion(NULL,@"无法创建视频解码器");return; }
        self->_needKey=NO;CMBlockBufferRef block=NULL;CMSampleBufferRef sample=NULL;CVPixelBufferRef image=NULL;
        OSStatus status=CMBlockBufferCreateWithMemoryBlock(NULL,NULL,avcc.length,NULL,NULL,0,avcc.length,0,&block);
        if(!status) status=CMBlockBufferReplaceDataBytes(avcc.bytes,block,0,avcc.length);
        size_t size=avcc.length;
        if(!status) status=CMSampleBufferCreateReady(NULL,block,self->_format,1,0,NULL,1,&size,&sample);
        if(!status) status=VTDecompressionSessionDecodeFrame(self->_session,sample,0,&image,NULL);
        if(sample) CFRelease(sample);if(block) CFRelease(block);
        if(status||!image) { [self resetSession];completion(NULL,[NSString stringWithFormat:@"视频解码失败 (%d)",(int)status]); }
        else completion(image,nil);
        if(image) CVPixelBufferRelease(image);
    }});
}
- (void)invalidate { dispatch_async(_queue,^{[self resetSession];self->_sps=nil;self->_pps=nil;}); }
- (void)dealloc { if(_session) { VTDecompressionSessionInvalidate(_session);CFRelease(_session); }if(_format) CFRelease(_format); }
@end
