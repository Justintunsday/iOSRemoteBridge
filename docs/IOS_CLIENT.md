# iPhone / iPad 控制端

这是 Objective-C + UIKit 编写的原生控制端，用另一台 iPhone / iPad 连接目前的越狱
iPhone。控制端支持 iOS / iPadOS 15 及以上，不需要越狱。被控端继续使用现有的
iOSRemoteBridge 守护进程和桌面插件。

## 安装与配对

1. 下载构建产物中的 `iOSRemoteBridge-Controller-unsigned.ipa`。
2. 使用 AltStore 或 Sideloadly，以你自己的 Apple ID 重新签名并安装。此 IPA 的临时
   签名仅用于打包检查，不含你的开发证书，也不能作为普通设备的直接安装签名。
   按签名工具提示完成设备信任、开发者模式及后续续签。
3. 将现有的 **client-config.json** 通过“文件”App、AirDrop 等方式传到控制端，
   在 App 中点“导入”。不要导入 device-config.json、server.p12 或服务器私钥。
   配对信息存入当前 App 的钥匙串，App 界面不显示令牌。
4. 控制端和被控端连接同一局域网，首次使用时允许 iOS 的“本地网络”访问。
   被控 iPhone 解锁亮屏，先断开 Windows 或其他客户端；服务同时只接受一个客户端。
5. 核对被控端地址，点“连接”。DHCP 地址变化后可以修改地址和端口，原来的证书
   指纹与令牌保持不变。每次连接都先验证证书指纹，再发送令牌。

## 操作

- 点击、按住拖动、长按和双指缩放直接映射到视频画面；黑边不产生远程点击。
- 底部按钮提供主屏幕、App 切换器、控制中心、音量和文字发送。
- “画质”可切换 30 / 60 FPS、原生 / 半分辨率，以及旋转显示画面。
- “全屏”隐藏上方和底部工具；画面右上角“工具”可恢复。
- 文字发送目前支持英文、数字、常用 ASCII 符号、换行和 Tab，最多 1024 字符。
  任意 Unicode / 中文文本传输尚未实现。
- 切到后台或锁定控制端会断开并释放触点；回到 App 后重新连接。网络中断时在
  当前前台会话中自动重连，旧输入不会重放。被控手机锁屏仍会结束会话。
- App 使用 VideoToolbox 解码、原生视频图层显示，协商最多三个在途视频帧；FPS
  是实际解码计数，RTT 是网络往返时间，不代表完整操作到显示的延迟。

iOS 控制端优先提供原生触控。Windows 的 FSR 实验模式与键鼠捕获不属于此版本。
App 不包含配对凭据，也不会改写被控端的锁屏或受保护内容行为。

## 在 macOS 上构建

安装 Xcode 和 CMake，先生成代码绘制的图标，再创建 Xcode 项目：

```sh
mkdir -p build/controller-tools
xcrun --sdk macosx clang -fobjc-arc -framework AppKit tools/ios_controller_icon.m -o build/controller-tools/create-icon
build/controller-tools/create-icon client/ios/Assets.xcassets/AppIcon.appiconset/AppIcon.png
cmake -S client/ios -B build/controller-device -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64
open build/controller-device/IRBController.xcodeproj
```

在 Xcode 中选择你自己的开发团队和控制端设备。源码中没有签名证书或 Apple ID。
CI 会编译 arm64 安装包，并在 iOS Simulator 中测试生产网络、解码和控件协议。
模拟器测试只使用临时证书、模拟服务器和人工生成的灰阶视频，不连接真实手机。
