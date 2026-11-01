#import "IRBAppDelegate.h"
#import "IRBSceneDelegate.h"
@implementation IRBAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options { (void)application;(void)options;return YES; }
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)application;(void)options;UISceneConfiguration *configuration=[[UISceneConfiguration alloc] initWithName:@"Controller" sessionRole:session.role];configuration.delegateClass=IRBSceneDelegate.class;return configuration;
}
@end
