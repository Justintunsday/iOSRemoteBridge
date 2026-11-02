#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@protocol IRBInterfaceActions <NSObject>
- (void)connectHost:(NSString *)host port:(NSString *)port NS_SWIFT_NAME(connect(host:port:));
- (void)disconnectRemote NS_SWIFT_NAME(disconnect());
- (void)importProfileURL:(NSURL *)url NS_SWIFT_NAME(importProfile(url:));
- (void)performOperation:(NSString *)operation NS_SWIFT_NAME(perform(operation:));
- (void)sendRemoteText:(NSString *)text NS_SWIFT_NAME(send(text:));
- (void)qualityFPS:(NSInteger)fps scale:(double)scale vsync:(BOOL)vsync NS_SWIFT_NAME(quality(fps:scale:vsync:));
- (void)rotateRemoteVideo NS_SWIFT_NAME(rotate());
- (void)cancelRemoteInput NS_SWIFT_NAME(cancelInput());
@end
NS_ASSUME_NONNULL_END
