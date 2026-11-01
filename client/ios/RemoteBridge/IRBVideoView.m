#import "IRBVideoView.h"
#import "IRBProtocol.h"
#import <AVFoundation/AVFoundation.h>
@implementation IRBVideoView {
    AVSampleBufferDisplayLayer *_display;
    CMVideoFormatDescriptionRef _format;
    CGSize _nativeSize;
    CGRect _imageRect;
    NSMutableDictionary<NSValue *,NSNumber *> *_fingers;
    BOOL _used[2],_dirty[2],_hasImage;
    CGPoint _positions[2];
    CFTimeInterval _lastSent[2];
    CADisplayLink *_link;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])) {
        self.backgroundColor=UIColor.blackColor;self.multipleTouchEnabled=YES;self.layer.cornerRadius=16;self.clipsToBounds=YES;
        _display=[AVSampleBufferDisplayLayer new];_display.videoGravity=AVLayerVideoGravityResizeAspect;[self.layer addSublayer:_display];
        _fingers=[NSMutableDictionary new];self.accessibilityLabel=@"远程 iPhone 画面";
    }return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];CGSize visible=_nativeSize;
    if(_rotation==90||_rotation==270) visible=CGSizeMake(visible.height,visible.width);
    _imageRect=visible.width>0?AVMakeRectWithAspectRatioInsideRect(visible,self.bounds):CGRectZero;
    [CATransaction begin];[CATransaction setDisableActions:YES];
    _display.position=CGPointMake(CGRectGetMidX(self.bounds),CGRectGetMidY(self.bounds));
    _display.bounds=CGRectMake(0,0,(_rotation==90||_rotation==270)?_imageRect.size.height:_imageRect.size.width,(_rotation==90||_rotation==270)?_imageRect.size.width:_imageRect.size.height);
    _display.affineTransform=CGAffineTransformMakeRotation(_rotation*M_PI/180.0);[CATransaction commit];
}
- (void)setRotation:(NSInteger)rotation { [self cancelTouches];_rotation=((rotation%360)+360)%360;[self setNeedsLayout]; }
- (void)setControlEnabled:(BOOL)enabled { if(!enabled) [self cancelTouches];_controlEnabled=enabled; }
- (void)display:(CVPixelBufferRef)image {
    CGSize size=CGSizeMake(CVPixelBufferGetWidth(image),CVPixelBufferGetHeight(image));
    if(!CGSizeEqualToSize(size,_nativeSize)) { _nativeSize=size;if(_format) {CFRelease(_format);_format=NULL;}[self setNeedsLayout];[self layoutIfNeeded]; }
    if(_display.status==AVQueuedSampleBufferRenderingStatusFailed) [_display flush];
    if(!_format&&CMVideoFormatDescriptionCreateForImageBuffer(NULL,image,&_format)) return;
    if(!_display.readyForMoreMediaData) return; // Drop only decoded pictures, never encoded reference frames.
    CMSampleTimingInfo timing={kCMTimeInvalid,kCMTimeInvalid,kCMTimeInvalid};CMSampleBufferRef sample=NULL;
    if(CMSampleBufferCreateReadyWithImageBuffer(NULL,image,_format,&timing,&sample)) return;
    CFArrayRef attachments=CMSampleBufferGetSampleAttachmentsArray(sample,true);
    if(attachments&&CFArrayGetCount(attachments)) CFDictionarySetValue((CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments,0),kCMSampleAttachmentKey_DisplayImmediately,kCFBooleanTrue);
    [_display enqueueSampleBuffer:sample];CFRelease(sample);_hasImage=YES;
}
- (void)clear { [self cancelTouches];[_display flushAndRemoveImage];_hasImage=NO; }
- (BOOL)normalizedPoint:(CGPoint)point clamp:(BOOL)clamp result:(CGPoint *)result {
    if(_imageRect.size.width<=0||_imageRect.size.height<=0||(!clamp&&!CGRectContainsPoint(_imageRect,point))) return NO;
    double x=MIN(1,MAX(0,(point.x-_imageRect.origin.x)/_imageRect.size.width));
    double y=MIN(1,MAX(0,(point.y-_imageRect.origin.y)/_imageRect.size.height));
    double naturalX,naturalY;if(!irb_unrotate(x,y,(int)_rotation,&naturalX,&naturalY)) return NO;
    *result=CGPointMake(naturalX,naturalY);return YES;
}
- (void)send:(int)finger phase:(int)phase {
    if(self.onTouch) self.onTouch(finger,phase,_positions[finger].x,_positions[finger].y);
    _dirty[finger]=NO;_lastSent[finger]=CACurrentMediaTime();
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(!_controlEnabled||!_hasImage) return;
    for(UITouch *touch in touches) {
        CGPoint point;if(![self normalizedPoint:[touch locationInView:self] clamp:NO result:&point]) continue;
        int finger=-1;for(int i=0;i<2;++i) if(!_used[i]) {finger=i;break;}if(finger<0) continue;
        _used[finger]=YES;_positions[finger]=point;_fingers[[NSValue valueWithNonretainedObject:touch]]=@(finger);[self send:finger phase:0];
    }
    if(_fingers.count&&!_link) { _link=[CADisplayLink displayLinkWithTarget:self selector:@selector(flushMoves:)];_link.preferredFramesPerSecond=60;[_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes]; }
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    for(UITouch *touch in touches) {
        NSNumber *slot=_fingers[[NSValue valueWithNonretainedObject:touch]];if(!slot) continue;int finger=slot.intValue;
        CGPoint point;if(![self normalizedPoint:[touch locationInView:self] clamp:YES result:&point]) continue;
        _positions[finger]=point;_dirty[finger]=YES;
        if(CACurrentMediaTime()-_lastSent[finger]>=1.0/60.0) [self send:finger phase:1];
    }
}
- (void)flushMoves:(CADisplayLink *)link {
    (void)link;for(int i=0;i<2;++i) if(_used[i]&&_dirty[i]&&CACurrentMediaTime()-_lastSent[i]>=1.0/60.0) [self send:i phase:1];
}
- (void)finish:(NSSet<UITouch *> *)touches {
    for(UITouch *touch in touches) {
        NSValue *key=[NSValue valueWithNonretainedObject:touch];NSNumber *slot=_fingers[key];if(!slot) continue;int finger=slot.intValue;
        CGPoint point;if([self normalizedPoint:[touch locationInView:self] clamp:YES result:&point]) _positions[finger]=point;
        [self send:finger phase:2];_used[finger]=NO;[_fingers removeObjectForKey:key];
    }
    if(!_fingers.count) { [_link invalidate];_link=nil; }
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event;[self finish:touches]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event;[self finish:touches]; }
- (void)cancelTouches {
    for(int i=0;i<2;++i) if(_used[i]) { [self send:i phase:2];_used[i]=NO; }
    [_fingers removeAllObjects];[_link invalidate];_link=nil;
}
- (void)dealloc { [_link invalidate];if(_format) CFRelease(_format); }
@end
