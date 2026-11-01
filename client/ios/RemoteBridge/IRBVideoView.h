#import <UIKit/UIKit.h>
#import <CoreVideo/CoreVideo.h>
NS_ASSUME_NONNULL_BEGIN
@interface IRBVideoView : UIView
@property(nonatomic) BOOL controlEnabled;
@property(nonatomic) NSInteger rotation;
@property(nonatomic,copy,nullable) void(^onTouch)(int finger,int phase,double x,double y);
- (void)display:(CVPixelBufferRef)image;
- (void)clear;
- (void)cancelTouches;
- (BOOL)normalizedPoint:(CGPoint)point clamp:(BOOL)clamp result:(CGPoint *)result;
@end
NS_ASSUME_NONNULL_END
