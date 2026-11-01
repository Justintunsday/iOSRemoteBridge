#import "IRBSceneDelegate.h"
#import "IRBControllerViewController.h"
#import "IRBSimulatorSmoke.h"
@implementation IRBSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;if(![scene isKindOfClass:UIWindowScene.class]) return;
    IRBPrepareSimulatorSmoke();IRBControllerViewController *controller=[IRBControllerViewController new];
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];self.window.rootViewController=controller;[self.window makeKeyAndVisible];[controller loadViewIfNeeded];IRBStartSimulatorSmoke(controller);
}
- (void)sceneWillResignActive:(UIScene *)scene { (void)scene;[(IRBControllerViewController *)self.window.rootViewController cancelInput]; }
- (void)sceneDidEnterBackground:(UIScene *)scene { (void)scene;[(IRBControllerViewController *)self.window.rootViewController suspend]; }
- (void)sceneDidDisconnect:(UIScene *)scene { (void)scene;[(IRBControllerViewController *)self.window.rootViewController suspend]; }
@end
