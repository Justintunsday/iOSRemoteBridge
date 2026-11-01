#import <Foundation/Foundation.h>
/* Runs in the entitled daemon, not inside SpringBoard. */
@interface IRBNativeHID : NSObject
@property(nonatomic,readonly) BOOL active;
@property(nonatomic,readonly) NSString *error;
- (BOOL)start;
- (BOOL)key:(uint16_t)usage down:(BOOL)down;
- (BOOL)mouseDX:(int16_t)dx dy:(int16_t)dy buttons:(uint8_t)buttons wheel:(int8_t)wheel;
- (NSDictionary *)diagnostics;
- (void)stop;
@end
