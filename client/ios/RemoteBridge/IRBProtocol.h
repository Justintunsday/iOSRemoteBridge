#import <Foundation/Foundation.h>
#include "../../../shared/irb_protocol.h"
#include <time.h>
NS_ASSUME_NONNULL_BEGIN
static inline uint64_t IRBClientNow(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW)/1000; }
@interface IRBPacket : NSObject
@property(nonatomic) irb_header header;
@property(nonatomic,copy) NSData *payload;
@end
@interface IRBPacketReader : NSObject
@property(nonatomic,readonly,nullable) NSString *error;
- (NSArray<IRBPacket *> *)append:(NSData *)data;
@end
FOUNDATION_EXPORT NSData *IRBClientPacket(uint8_t type,uint32_t sequence,uint64_t pts,NSData * _Nullable payload);
FOUNDATION_EXPORT NSDictionary * _Nullable IRBClientJSON(NSData *data);
NS_ASSUME_NONNULL_END
