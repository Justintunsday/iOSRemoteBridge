#import <UIKit/UIKit.h>
#import "IRBSession.h"
#import "IRBVideoView.h"
@interface IRBControllerViewController : UIViewController
@property(nonatomic,readonly) IRBSession *session;
@property(nonatomic,readonly) IRBVideoView *videoView;
- (void)suspend;
- (void)cancelInput;
@end
