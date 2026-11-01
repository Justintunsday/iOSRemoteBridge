#import <UIKit/UIKit.h>
#import <CoreVideo/CoreVideo.h>
@interface IRBCapture : NSObject
@property(nonatomic,readonly) int width;
@property(nonatomic,readonly) int height;
@property(nonatomic,readonly) NSString *error;
- (BOOL)prepareWithScale:(double)scale;
/* Caller owns +1; no buffer is reused while VideoToolbox holds a reference. */
- (CVPixelBufferRef)copyFrame CF_RETURNS_RETAINED;
@end
