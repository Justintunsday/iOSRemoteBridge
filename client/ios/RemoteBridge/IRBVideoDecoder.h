#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
NS_ASSUME_NONNULL_BEGIN
FOUNDATION_EXPORT NSArray<NSData *> *IRBSplitAnnexB(NSData *data);
@interface IRBVideoDecoder : NSObject
- (void)decode:(NSData *)data keyframe:(BOOL)key completion:(void(^)(CVPixelBufferRef _Nullable image,NSString * _Nullable error))completion;
- (void)invalidate;
@end
NS_ASSUME_NONNULL_END
