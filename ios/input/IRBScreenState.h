#import <Foundation/Foundation.h>
@interface IRBScreenState : NSObject
+ (BOOL)locked;
+ (BOOL)screenKnown;
+ (BOOL)screenOn;
+ (BOOL)wakeAvailable;
+ (BOOL)wake;
@end
