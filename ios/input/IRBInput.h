#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
@interface IRBInput : NSObject
@property(nonatomic,readonly) BOOL ready;
@property(nonatomic,readonly) NSString *status;
- (BOOL)touch:(int)finger phase:(int)phase x:(double)x y:(double)y;
- (BOOL)key:(uint16_t)usage down:(BOOL)down;
- (BOOL)button:(NSString *)button;
- (BOOL)typeASCII:(NSString *)text;
- (BOOL)supportsText:(NSString *)text;
- (void)releaseAll;
@end
