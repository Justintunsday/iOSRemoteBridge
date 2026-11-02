#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import "IRBProfile.h"
NS_ASSUME_NONNULL_BEGIN
typedef NS_ENUM(NSInteger,IRBSessionState) { IRBSessionIdle,IRBSessionConnecting,IRBSessionStreaming };
@interface IRBSession : NSObject
@property(nonatomic,copy,nullable) void(^onState)(IRBSessionState state,NSString *message);
@property(nonatomic,copy,nullable) void(^onFrame)(CVPixelBufferRef image,uint64_t pts);
@property(nonatomic,copy,nullable) void(^onStats)(NSDictionary *stats);
@property(nonatomic,copy,nullable) void(^onResponse)(NSDictionary *response);
@property(nonatomic,copy,nullable) void(^onDeviceState)(NSDictionary *device);
- (void)connect:(IRBProfile *)profile fps:(int)fps control:(BOOL)control;
- (void)disconnect;
- (void)command:(NSDictionary *)request;
@end
NS_ASSUME_NONNULL_END
