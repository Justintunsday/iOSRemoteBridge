#import <UIKit/UIKit.h>
#import "IRBSession.h"
#import "IRBVideoView.h"
#import <TargetConditionals.h>
@interface IRBControllerViewController : UIViewController
@property(nonatomic,readonly) IRBSession *session;
@property(nonatomic,readonly) IRBVideoView *videoView;
- (void)suspend;
- (void)cancelInput;
#if TARGET_OS_SIMULATOR
- (void)previewInterface:(NSString *)scene;
#endif
@end
