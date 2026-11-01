#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
@interface IRBEncoder : NSObject
@property(nonatomic,copy) void (^output)(NSData *,BOOL,uint64_t,double,OSStatus);
@property(nonatomic,readonly) NSString *error;
@property(nonatomic,readonly) NSString *hardwareStatus;
@property(nonatomic,readonly) BOOL lowLatency;
- (BOOL)startWidth:(int)width height:(int)height fps:(int)fps bitrate:(int)bitrate;
- (OSStatus)encode:(CVPixelBufferRef)buffer pts:(uint64_t)pts forceKeyframe:(BOOL)key;
- (BOOL)setBitrate:(int)bitrate;
- (BOOL)setFPS:(int)fps;
- (void)stop;
@end
