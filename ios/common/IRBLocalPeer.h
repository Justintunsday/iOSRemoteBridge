#import "IRBCommon.h"
@interface IRBLocalPeer : NSObject
@property(nonatomic,copy) void (^received)(irb_header,NSData *);
@property(nonatomic,copy) dispatch_block_t disconnected;
- (instancetype)initWithSocket:(int)fd;
- (void)start;
- (void)send:(uint8_t)type flags:(uint8_t)flags pts:(uint64_t)pts data:(NSData *)data;
- (void)close;
@end
