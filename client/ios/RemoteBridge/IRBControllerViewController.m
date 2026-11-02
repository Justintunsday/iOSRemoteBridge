#import "IRBControllerViewController.h"
#import "IRBInterfaceActions.h"
#import "iOSRemoteBridge-Swift.h"

@interface IRBControllerViewController () <IRBInterfaceActions>
@end
@implementation IRBControllerViewController {
    IRBProfile *_profile;
    IRBInterfaceHost *_interface;
    IRBSessionState _state;
    int _fps;
    double _scale;
    BOOL _inputAllowed;
    uint64_t _minimumVisiblePTS;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    [defaults registerDefaults:@{@"vsync":@YES}];
    _fps=[defaults integerForKey:@"fps"]==30?30:60;_scale=[defaults boolForKey:@"half"]?.5:1;
    _profile=IRBProfileStore.load;_session=[IRBSession new];_videoView=[IRBVideoView new];
    _videoView.vSyncEnabled=[defaults boolForKey:@"vsync"];
    _interface=[[IRBInterfaceHost alloc] initWithVideoView:_videoView actions:self host:_profile.host?:@"" port:[NSString stringWithFormat:@"%u",_profile?_profile.port:7443] paired:_profile!=nil fps:_fps scale:_scale vsync:_videoView.vSyncEnabled];
    UIViewController *host=_interface.viewController;
    [self addChildViewController:host];host.view.translatesAutoresizingMaskIntoConstraints=NO;[self.view addSubview:host.view];
    [NSLayoutConstraint activateConstraints:@[[host.view.topAnchor constraintEqualToAnchor:self.view.topAnchor],[host.view.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],[host.view.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],[host.view.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor]]];
    [host didMoveToParentViewController:self];
    __weak IRBControllerViewController *weak=self;
    _videoView.onTouch=^(int finger,int phase,double x,double y) {[weak.session command:@{@"op":@"touch",@"finger":@(finger),@"phase":@(phase),@"x":@(x),@"y":@(y)}];};
    _session.onState=^(IRBSessionState state,NSString *message) {
        IRBControllerViewController *owner=weak;if(!owner) return;owner->_state=state;
        owner->_videoView.controlEnabled=state==IRBSessionStreaming&&owner->_inputAllowed;
        UIApplication.sharedApplication.idleTimerDisabled=state==IRBSessionStreaming;
        if(state!=IRBSessionStreaming) [owner->_videoView clear];else [owner applyQuality];
        [owner->_interface updateConnectionState:state message:message];
        [owner setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
    };
    _session.onFrame=^(CVPixelBufferRef image,uint64_t pts) {(void)pts;[weak.videoView display:image];};
    _session.onDeviceState=^(NSDictionary *device) {
        IRBControllerViewController *owner=weak;if(!owner) return;
        uint64_t minimum=[device[@"min_visible_pts"] unsignedLongLongValue];
        if(minimum!=owner->_minimumVisiblePTS) { [owner cancelInput];[owner->_videoView clear]; }
        owner->_minimumVisiblePTS=minimum;
        owner->_inputAllowed=device[@"input_allowed"]?[device[@"input_allowed"] boolValue]:YES;
        owner->_videoView.controlEnabled=owner->_state==IRBSessionStreaming&&owner->_inputAllowed;
        [owner->_interface updateDeviceLocked:[device[@"locked"] boolValue] screenOff:[device[@"display_on"] isEqual:@NO] inputAllowed:owner->_inputAllowed wakeSupported:[device[@"wake_supported"] boolValue]];
    };
    _session.onStats=^(NSDictionary *stats) {
        IRBControllerViewController *owner=weak;if(owner) [owner->_interface updateFPS:[stats[@"fps"] doubleValue] rtt:[stats[@"rtt_ms"] doubleValue]];
    };
    _session.onResponse=^(NSDictionary *reply) {
        IRBControllerViewController *owner=weak;id ok=reply[@"ok"];
        if(owner&&[ok isKindOfClass:NSNumber.class]&&![ok boolValue]&&[reply[@"error"] isKindOfClass:NSString.class]) [owner->_interface showMessage:reply[@"error"]];
    };
}
- (void)connectHost:(NSString *)host port:(NSString *)port {
    [self.view endEditing:YES];if(_state!=IRBSessionIdle) return;
    if(!_profile) {[_interface showMessage:@"请先导入 client-config.json 配对文件"];return;}
    NSError *error=nil;IRBProfile *profile=[_profile withHost:host port:port error:&error];
    if(!profile||![IRBProfileStore save:profile error:&error]) {[_interface showMessage:error.localizedDescription];return;}
    _profile=profile;[_session connect:profile fps:_fps control:YES];
}
- (void)disconnectRemote { [self cancelInput];[_session disconnect]; }
- (void)importProfileURL:(NSURL *)url {
    [self disconnectRemote];BOOL scoped=[url startAccessingSecurityScopedResource];NSError *error=nil;NSNumber *size=nil;
    [url getResourceValue:&size forKey:NSURLFileSizeKey error:&error];
    NSData *data=(size&&size.unsignedLongLongValue<=65536)?[NSData dataWithContentsOfURL:url options:0 error:&error]:nil;
    IRBProfile *profile=data?[IRBProfile fromData:data error:&error]:nil;if(scoped) [url stopAccessingSecurityScopedResource];
    if(!profile||![IRBProfileStore save:profile error:&error]) {[_interface showMessage:error.localizedDescription?:@"请选择不超过 64 KB 的 client-config.json"];return;}
    _profile=profile;[_interface updateProfileHost:profile.host port:[NSString stringWithFormat:@"%u",profile.port]];
}
- (void)performOperation:(NSString *)operation {
    if(_state!=IRBSessionStreaming) return;
    NSSet *operations=[NSSet setWithArray:@[@"wake_screen",@"press_home",@"app_switcher",@"control_center",@"volume_down",@"volume_up"]];
    if(![operations containsObject:operation]) return;
    [self cancelInput];[_session command:@{@"op":operation}];
}
- (void)sendRemoteText:(NSString *)text {
    if(_state!=IRBSessionStreaming||!text.length) return;
    NSMutableCharacterSet *allowed=[[NSCharacterSet characterSetWithRange:NSMakeRange(32,95)] mutableCopy];[allowed addCharactersInString:@"\n\t"];
    if(text.length>1024||[text rangeOfCharacterFromSet:allowed.invertedSet].location!=NSNotFound) {[_interface showMessage:@"文字发送支持英文、数字和常用符号，最多 1024 字符"];return;}
    [self cancelInput];[_session command:@{@"op":@"type_text",@"text":text}];
}
- (void)qualityFPS:(NSInteger)fps scale:(double)scale vsync:(BOOL)vsync {
    if((fps!=30&&fps!=60)||(scale!=1&&scale!=.5)) return;
    BOOL changed=_fps!=fps||_scale!=scale;_fps=(int)fps;_scale=scale;_videoView.vSyncEnabled=vsync;
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    [defaults setInteger:fps forKey:@"fps"];[defaults setBool:scale!=1 forKey:@"half"];[defaults setBool:vsync forKey:@"vsync"];
    if(changed) [self applyQuality];
}
- (void)applyQuality {if(_state==IRBSessionStreaming) [_session command:@{@"op":@"configure",@"fps":@(_fps),@"scale":@(_scale),@"bitrate":@8000000}];}
- (void)rotateRemoteVideo { [self cancelInput];_videoView.rotation=(_videoView.rotation+90)%360; }
- (void)cancelRemoteInput { [self cancelInput]; }
- (void)cancelInput { [_videoView cancelTouches]; }
#if TARGET_OS_SIMULATOR
- (void)previewInterface:(NSString *)scene { [_interface setSimulatorPreview:scene]; }
#endif
- (void)suspend { [self cancelInput];[_session disconnect];[self.view endEditing:YES];UIApplication.sharedApplication.idleTimerDisabled=NO; }
- (UIViewController *)childViewControllerForStatusBarHidden { return _interface.viewController; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return _state==IRBSessionStreaming?UIRectEdgeAll:UIRectEdgeNone; }
@end
