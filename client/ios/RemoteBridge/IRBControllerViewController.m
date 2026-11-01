#import "IRBControllerViewController.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
@interface IRBControllerViewController () <UIDocumentPickerDelegate,UITextFieldDelegate>
@end
@implementation IRBControllerViewController {
    IRBProfile *_profile;
    UIStackView *_root,*_header,*_hostRow,*_textRow;
    UIScrollView *_tools;
    UITextField *_host,*_port,*_text;
    UILabel *_status,*_stats;
    UIButton *_connect,*_fullScreen,*_settings;
    NSMutableArray<UIButton *> *_remoteButtons;
    IRBSessionState _state;
    BOOL _fullscreen;
    BOOL _editingHost,_editingText;
    UIView *_editingSpacer;
    int _fps;
    double _scale;
}
- (UIButton *)button:(NSString *)title symbol:(NSString *)symbol action:(SEL)action {
    UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];UIButtonConfiguration *configuration=UIButtonConfiguration.plainButtonConfiguration;
    configuration.title=title;configuration.image=[UIImage systemImageNamed:symbol];configuration.imagePadding=5;
    button.configuration=configuration;[button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];return button;
}
- (UITextField *)field:(NSString *)placeholder {
    UITextField *field=[UITextField new];field.placeholder=placeholder;field.borderStyle=UITextBorderStyleRoundedRect;
    field.autocorrectionType=UITextAutocorrectionTypeNo;field.autocapitalizationType=UITextAutocapitalizationTypeNone;
    field.smartQuotesType=UITextSmartQuotesTypeNo;field.smartDashesType=UITextSmartDashesTypeNo;field.spellCheckingType=UITextSpellCheckingTypeNo;
    field.delegate=self;return field;
}
- (void)viewDidLoad {
    [super viewDidLoad];self.view.backgroundColor=UIColor.systemBackgroundColor;self.view.tintColor=[UIColor colorWithRed:.1 green:.48 blue:.94 alpha:1];
    _fps=[NSUserDefaults.standardUserDefaults integerForKey:@"fps"]==30?30:60;_scale=[NSUserDefaults.standardUserDefaults boolForKey:@"half"]?.5:1;
    _session=[IRBSession new];_profile=IRBProfileStore.load;_remoteButtons=[NSMutableArray new];
    _root=[UIStackView new];_root.axis=UILayoutConstraintAxisVertical;_root.spacing=8;_root.translatesAutoresizingMaskIntoConstraints=NO;[self.view addSubview:_root];
    [NSLayoutConstraint activateConstraints:@[[_root.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:6],[_root.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],[_root.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-12],[_root.bottomAnchor constraintEqualToAnchor:self.view.keyboardLayoutGuide.topAnchor constant:-6]]];
    _header=[UIStackView new];_header.alignment=UIStackViewAlignmentCenter;_header.spacing=8;
    UILabel *title=[UILabel new];title.text=@"iOSRemoteBridge";title.font=[UIFont boldSystemFontOfSize:21];[title setContentCompressionResistancePriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
    [_header addArrangedSubview:title];UIButton *import=[self button:@"导入" symbol:@"square.and.arrow.down" action:@selector(importProfile)];[_header addArrangedSubview:import];
    _connect=[self button:@"连接" symbol:@"link" action:@selector(toggleConnection)];_connect.configuration=UIButtonConfiguration.filledButtonConfiguration;[_connect setTitle:@"连接" forState:UIControlStateNormal];[_header addArrangedSubview:_connect];[_root addArrangedSubview:_header];
    _hostRow=[UIStackView new];_hostRow.spacing=8;_host=[self field:@"被控 iPhone 的局域网地址"];_host.keyboardType=UIKeyboardTypeURL;
    _port=[self field:@"7443"];_port.keyboardType=UIKeyboardTypeNumberPad;[_port.widthAnchor constraintEqualToConstant:76].active=YES;
    _host.text=_profile.host;_port.text=[NSString stringWithFormat:@"%u",_profile?_profile.port:7443];[_hostRow addArrangedSubview:_host];[_hostRow addArrangedSubview:_port];[_root addArrangedSubview:_hostRow];
    _status=[UILabel new];_status.font=[UIFont systemFontOfSize:12];_status.textColor=UIColor.secondaryLabelColor;_status.numberOfLines=2;
    _status.text=_profile?@"配对已保存，连接前请解锁被控手机":@"导入 client-config.json 后即可连接";[_root addArrangedSubview:_status];
    _editingSpacer=[UIView new];_editingSpacer.hidden=YES;[_root addArrangedSubview:_editingSpacer];
    _stats=[UILabel new];_stats.font=[UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightRegular];_stats.textColor=UIColor.secondaryLabelColor;_stats.text=@"视频尚未连接";[_root addArrangedSubview:_stats];
    _videoView=[IRBVideoView new];[_root addArrangedSubview:_videoView];NSLayoutConstraint *minimumVideoHeight=[_videoView.heightAnchor constraintGreaterThanOrEqualToConstant:60];minimumVideoHeight.priority=999;minimumVideoHeight.active=YES;
    [_videoView setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisVertical];
    _fullScreen=[self button:@"全屏" symbol:@"arrow.up.left.and.arrow.down.right" action:@selector(toggleFullscreen)];_fullScreen.backgroundColor=[UIColor colorWithWhite:.12 alpha:.8];_fullScreen.tintColor=UIColor.whiteColor;_fullScreen.layer.cornerRadius=12;
    _fullScreen.translatesAutoresizingMaskIntoConstraints=NO;[_videoView addSubview:_fullScreen];[NSLayoutConstraint activateConstraints:@[[_fullScreen.topAnchor constraintEqualToAnchor:_videoView.topAnchor constant:8],[_fullScreen.trailingAnchor constraintEqualToAnchor:_videoView.trailingAnchor constant:-8]]];
    _tools=[UIScrollView new];_tools.showsHorizontalScrollIndicator=NO;NSLayoutConstraint *toolsHeight=[_tools.heightAnchor constraintEqualToConstant:58];toolsHeight.priority=999;toolsHeight.active=YES;
    UIStackView *toolRow=[UIStackView new];toolRow.spacing=3;toolRow.translatesAutoresizingMaskIntoConstraints=NO;[_tools addSubview:toolRow];
    [NSLayoutConstraint activateConstraints:@[[toolRow.leadingAnchor constraintEqualToAnchor:_tools.contentLayoutGuide.leadingAnchor],[toolRow.trailingAnchor constraintEqualToAnchor:_tools.contentLayoutGuide.trailingAnchor],[toolRow.topAnchor constraintEqualToAnchor:_tools.contentLayoutGuide.topAnchor],[toolRow.bottomAnchor constraintEqualToAnchor:_tools.contentLayoutGuide.bottomAnchor],[toolRow.heightAnchor constraintEqualToAnchor:_tools.frameLayoutGuide.heightAnchor]]];
    NSArray *titles=@[@"主屏幕",@"切换器",@"控制中心",@"音量−",@"音量＋",@"文字",@"画质"];
    NSArray *symbols=@[@"house",@"square.stack",@"switch.2",@"speaker.minus",@"speaker.plus",@"keyboard",@"slider.horizontal.3"];
    NSArray *actions=@[NSStringFromSelector(@selector(home)),NSStringFromSelector(@selector(switcher)),NSStringFromSelector(@selector(controlCenter)),NSStringFromSelector(@selector(volumeDown)),NSStringFromSelector(@selector(volumeUp)),NSStringFromSelector(@selector(toggleText)),NSStringFromSelector(@selector(quality))];
    for(NSUInteger i=0;i<titles.count;++i) { UIButton *button=[self button:titles[i] symbol:symbols[i] action:NSSelectorFromString(actions[i])];UIButtonConfiguration *configuration=button.configuration;configuration.imagePlacement=NSDirectionalRectEdgeTop;configuration.imagePadding=2;button.configuration=configuration;[button.widthAnchor constraintGreaterThanOrEqualToConstant:66].active=YES;[toolRow addArrangedSubview:button];if(i<5) [_remoteButtons addObject:button];if(i==6) _settings=button; }
    [_root addArrangedSubview:_tools];_textRow=[UIStackView new];_textRow.spacing=8;_text=[self field:@"英文、数字或符号"];_text.returnKeyType=UIReturnKeySend;
    [_textRow addArrangedSubview:[self button:@"收起" symbol:@"chevron.down" action:@selector(toggleText)]];
    [_textRow addArrangedSubview:_text];UIButton *send=[self button:@"发送" symbol:@"paperplane" action:@selector(sendText)];[_textRow addArrangedSubview:send];[_remoteButtons addObject:send];_textRow.hidden=YES;[_root addArrangedSubview:_textRow];
    [self remoteEnabled:NO];__weak IRBControllerViewController *weak=self;
    _videoView.onTouch=^(int finger,int phase,double x,double y) {[weak.session command:@{@"op":@"touch",@"finger":@(finger),@"phase":@(phase),@"x":@(x),@"y":@(y)}];};
    _session.onState=^(IRBSessionState state,NSString *message) {
        IRBControllerViewController *owner=weak;if(!owner) return;owner->_state=state;owner->_status.text=message;
        [owner->_connect setTitle:state==IRBSessionIdle?@"连接":state==IRBSessionConnecting?@"取消":@"断开" forState:UIControlStateNormal];
        owner->_host.enabled=owner->_port.enabled=state==IRBSessionIdle;[owner remoteEnabled:state==IRBSessionStreaming];UIApplication.sharedApplication.idleTimerDisabled=state==IRBSessionStreaming;
        if(state!=IRBSessionStreaming) { [owner->_videoView clear];owner->_stats.text=@"视频尚未连接"; }
        else if(owner->_scale!=1) [owner applyQuality];[owner setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
    };
    _session.onFrame=^(CVPixelBufferRef image,uint64_t pts) {(void)pts;[weak.videoView display:image];};
    _session.onStats=^(NSDictionary *stats) {IRBControllerViewController *owner=weak;if(!owner) return;owner->_stats.text=[NSString stringWithFormat:@"%.1f FPS · RTT %.1f ms · %d FPS%@",[stats[@"fps"] doubleValue],[stats[@"rtt_ms"] doubleValue],owner->_fps,owner->_scale==1?@" 原生分辨率":@" 半分辨率"];};
    _session.onResponse=^(NSDictionary *reply) {IRBControllerViewController *owner=weak;if(!owner) return;id ok=reply[@"ok"];if([ok isKindOfClass:NSNumber.class]&&![ok boolValue]&&[reply[@"error"] isKindOfClass:NSString.class]) owner->_status.text=reply[@"error"];};
}
- (void)remoteEnabled:(BOOL)enabled { _videoView.controlEnabled=enabled;for(UIButton *button in _remoteButtons) button.enabled=enabled; }
- (void)toggleConnection {
    [self.view endEditing:YES];if(_state!=IRBSessionIdle) {[_session disconnect];return;}
    if(!_profile) { [self importProfile];return; }NSError *error=nil;IRBProfile *profile=[_profile withHost:_host.text?:@"" port:_port.text?:@"" error:&error];
    if(!profile||![IRBProfileStore save:profile error:&error]) { _status.text=error.localizedDescription;return; }_profile=profile;[_session connect:profile fps:_fps control:YES];
}
- (void)importProfile {
    [_videoView cancelTouches];[_session disconnect];[self.view endEditing:YES];
    UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeJSON] asCopy:NO];picker.delegate=self;picker.allowsMultipleSelection=NO;[self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;NSURL *url=urls.firstObject;if(!url) return;BOOL scoped=[url startAccessingSecurityScopedResource];NSError *error=nil;
    NSNumber *size=nil;[url getResourceValue:&size forKey:NSURLFileSizeKey error:&error];NSData *data=(size&&size.unsignedLongLongValue<=65536)?[NSData dataWithContentsOfURL:url options:0 error:&error]:nil;
    IRBProfile *profile=data?[IRBProfile fromData:data error:&error]:nil;if(scoped) [url stopAccessingSecurityScopedResource];
    if(!profile||![IRBProfileStore save:profile error:&error]) { _status.text=error.localizedDescription?:@"请选择不超过 64 KB 的 client-config.json";return; }
    _profile=profile;_host.text=profile.host;_port.text=[NSString stringWithFormat:@"%u",profile.port];_status.text=@"配对已导入，可以连接";
}
- (void)operation:(NSString *)operation { [_videoView cancelTouches];[_session command:@{@"op":operation}]; }
- (void)home { [self operation:@"press_home"]; }
- (void)switcher { [self operation:@"app_switcher"]; }
- (void)controlCenter { [self operation:@"control_center"]; }
- (void)volumeDown { [self operation:@"volume_down"]; }
- (void)volumeUp { [self operation:@"volume_up"]; }
- (void)toggleText { [_videoView cancelTouches];_textRow.hidden=!_textRow.hidden;if(!_textRow.hidden) [_text becomeFirstResponder];else [self.view endEditing:YES]; }
- (void)sendText {
    NSString *text=_text.text?:@"";if(!text.length) return;
    NSCharacterSet *supported=[NSCharacterSet characterSetWithRange:NSMakeRange(32,95)];NSMutableCharacterSet *allowed=[supported mutableCopy];[allowed addCharactersInString:@"\n\t"];
    if(text.length>1024||[text rangeOfCharacterFromSet:allowed.invertedSet].location!=NSNotFound) { _status.text=@"当前文字发送支持英文、数字和常用符号，最多 1024 字符";return; }
    [_videoView cancelTouches];[_session command:@{@"op":@"type_text",@"text":text}];_text.text=@"";
}
- (BOOL)textFieldShouldReturn:(UITextField *)field { if(field==_text) [self sendText];else [field resignFirstResponder];return YES; }
- (void)updateChrome {
    BOOL compact=_fullscreen||_editingText;
    _header.hidden=_hostRow.hidden=_status.hidden=compact;_stats.hidden=compact||_editingHost;
    _tools.hidden=compact||_editingHost;_videoView.hidden=_editingHost;_editingSpacer.hidden=!_editingHost;
}
- (void)textFieldDidBeginEditing:(UITextField *)field { _editingText=field==_text;_editingHost=field==_host||field==_port;[self updateChrome]; }
- (void)textFieldDidEndEditing:(UITextField *)field { (void)field;_editingText=_editingHost=NO;[self updateChrome]; }
- (void)toggleFullscreen {
    [_videoView cancelTouches];_fullscreen=!_fullscreen;
    if(_fullscreen) {_textRow.hidden=YES;[self.view endEditing:YES];}
    [self updateChrome];
    [_fullScreen setTitle:_fullscreen?@"工具":@"全屏" forState:UIControlStateNormal];[self setNeedsStatusBarAppearanceUpdate];
}
- (BOOL)prefersStatusBarHidden { return _fullscreen; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return _state==IRBSessionStreaming?UIRectEdgeAll:UIRectEdgeNone; }
- (void)applyQuality { if(_state==IRBSessionStreaming) [_session command:@{@"op":@"configure",@"fps":@(_fps),@"scale":@(_scale),@"bitrate":@8000000}]; }
- (void)quality {
    [_videoView cancelTouches];UIAlertController *sheet=[UIAlertController alertControllerWithTitle:@"画质" message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    for(NSNumber *fps in @[@60,@30]) for(NSNumber *scale in @[@1,@.5]) {
        NSString *title=[NSString stringWithFormat:@"%@ FPS · %@",fps,scale.doubleValue==1?@"原生分辨率":@"半分辨率"];
        [sheet addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {(void)action;self->_fps=fps.intValue;self->_scale=scale.doubleValue;[NSUserDefaults.standardUserDefaults setInteger:self->_fps forKey:@"fps"];[NSUserDefaults.standardUserDefaults setBool:self->_scale!=1 forKey:@"half"];[self applyQuality];}]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"旋转画面" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {(void)action;self->_videoView.rotation=(self->_videoView.rotation+90)%360;}]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:nil]];sheet.popoverPresentationController.sourceView=_settings;sheet.popoverPresentationController.sourceRect=_settings.bounds;[self presentViewController:sheet animated:YES completion:nil];
}
- (void)cancelInput { [_videoView cancelTouches]; }
- (void)suspend { [self cancelInput];[_session disconnect];[self.view endEditing:YES];UIApplication.sharedApplication.idleTimerDisabled=NO; }
@end
