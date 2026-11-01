#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface IRBProfile : NSObject
@property(nonatomic,readonly,copy) NSString *host;
@property(nonatomic,readonly) uint16_t port;
@property(nonatomic,readonly,copy) NSString *token;
@property(nonatomic,readonly,copy) NSString *certificatePin;
@property(nonatomic,readonly,copy) NSData *pinBytes;
+ (nullable instancetype)fromData:(NSData *)data error:(NSError **)error;
- (nullable IRBProfile *)withHost:(NSString *)host port:(NSString *)port error:(NSError **)error;
- (NSData *)JSONData;
@end
@interface IRBProfileStore : NSObject
+ (nullable IRBProfile *)load;
+ (BOOL)save:(IRBProfile *)profile error:(NSError **)error;
+ (void)remove;
@end
NS_ASSUME_NONNULL_END
