#import <Foundation/Foundation.h>
#import "../../shared/irb_protocol.h"
#include <time.h>
#define IRB_DIRECTORY @"/var/jb/var/mobile/Library/iOSRemoteBridge"
#define IRB_SOCKET IRB_DIRECTORY @"/bridge.sock"
static inline uint64_t IRBNow(void) {
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000;
}
static inline NSData *IRBJSON(id object) {
    return [NSJSONSerialization dataWithJSONObject:object options:0 error:NULL];
}
static inline NSDictionary *IRBDictionary(NSData *data) {
    if(!data) return nil;
    id obj=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    return [obj isKindOfClass:NSDictionary.class]?obj:nil;
}
static inline NSNumber *IRBNumber(id value) {
    return [value isKindOfClass:NSNumber.class]?value:nil;
}
static inline double IRBReal(id value) {
    return IRBNumber(value)?[value doubleValue]:NAN;
}
static inline NSData *IRBPacket(uint8_t type, uint8_t flags, uint32_t sequence,
                               uint64_t pts, NSData *data) {
    uint8_t h[IRB_HEADER_SIZE];
    irb_header header={type,flags,(uint32_t)data.length,sequence,pts}; irb_pack(h,header);
    NSMutableData *out=[NSMutableData dataWithBytes:h length:sizeof(h)];
    if(data) [out appendData:data];
    return out;
}
