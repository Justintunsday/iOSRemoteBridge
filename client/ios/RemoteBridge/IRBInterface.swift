import SwiftUI
import UIKit
import UniformTypeIdentifiers

@MainActor
private final class RemoteModel: ObservableObject {
    let video: IRBVideoView
    weak var actions: IRBInterfaceActions?
    @Published var host: String
    @Published var port: String
    @Published var paired: Bool
    @Published var state = 0
    @Published var message: String
    @Published var fps: Int
    @Published var scale: Double
    @Published var vsync: Bool
    @Published var decodedFPS = 0.0
    @Published var rtt = 0.0
    @Published var fullscreen = false
    @Published var showSettings = false
    @Published var showImporter = false
    @Published var showText = false
    @Published var floatingHome: Bool
    @Published var locked = false
    @Published var screenOff = false
    @Published var inputAllowed = false
    @Published var wakeSupported = false
    var connected: Bool { state == 2 }
    var canControl: Bool { connected && inputAllowed }

    init(video: IRBVideoView, actions: IRBInterfaceActions, host: String, port: String, paired: Bool, fps: Int, scale: Double, vsync: Bool) {
        self.video = video; self.actions = actions; self.host = host; self.port = port
        self.paired = paired; self.fps = fps; self.scale = scale; self.vsync = vsync
        self.message = paired ? "配对已保存，可以连接设备" : "导入配对文件，连接你的 iPhone"
        self.floatingHome = UserDefaults.standard.object(forKey: "floatingHome") as? Bool ?? true
    }
    func connect() {
        if state != 0 { actions?.disconnect(); return }
        if !paired { beginImport(); return }
        actions?.connect(host: host, port: port)
    }
    func beginImport() {
        actions?.cancelInput(); actions?.disconnect(); showImporter = true
    }
    func perform(_ operation: String) {
        guard connected && (inputAllowed || operation == "wake_screen") else { return }
        actions?.perform(operation: operation)
    }
    func preferencesChanged() { actions?.quality(fps: fps, scale: scale, vsync: vsync) }
    func toggleFullscreen() { actions?.cancelInput(); fullscreen.toggle() }
    func openText() { actions?.cancelInput(); showText = true }
}

// Objective-C owns networking, pairing and decoding; SwiftUI observes only UI
// state and the once-per-second statistics. Video pixels never become @State.
@MainActor
@objc(IRBInterfaceHost)
final class IRBInterfaceHost: NSObject {
    private let model: RemoteModel
    @objc let viewController: UIViewController

    @objc(initWithVideoView:actions:host:port:paired:fps:scale:vsync:)
    init(videoView: IRBVideoView, actions: IRBInterfaceActions, host: String, port: String, paired: Bool, fps: Int, scale: Double, vsync: Bool) {
        let model = RemoteModel(video: videoView, actions: actions, host: host, port: port, paired: paired, fps: fps, scale: scale, vsync: vsync)
        self.model = model
        self.viewController = UIHostingController(rootView: RemoteScreen(model: model))
        super.init()
        viewController.view.backgroundColor = .systemGroupedBackground
    }
    @objc(updateConnectionState:message:)
    func updateConnectionState(_ state: Int, message: String) {
        model.state = state; model.message = message
        if state != 2 {
            model.decodedFPS = 0; model.rtt = 0; model.showText = false
            model.locked = false; model.screenOff = false; model.inputAllowed = false; model.wakeSupported = false
        }
    }
    @objc(updateDeviceLocked:screenOff:inputAllowed:wakeSupported:)
    func updateDeviceLocked(_ locked: Bool, screenOff: Bool, inputAllowed: Bool, wakeSupported: Bool) {
        model.locked = locked; model.screenOff = screenOff; model.inputAllowed = inputAllowed; model.wakeSupported = wakeSupported
        if !inputAllowed { model.showText = false }
    }
    @objc(updateProfileHost:port:)
    func updateProfileHost(_ host: String, port: String) {
        model.host = host; model.port = port; model.paired = true
        model.message = "配对已导入，可以连接"
    }
    @objc(updateFPS:rtt:)
    func updateFPS(_ fps: Double, rtt: Double) { model.decodedFPS = fps; model.rtt = rtt }
    @objc(showMessage:)
    func showMessage(_ message: String) { model.message = message }

    #if targetEnvironment(simulator)
    @objc(setSimulatorPreview:)
    func setSimulatorPreview(_ scene: String) {
        model.showSettings = scene == "settings"
        model.fullscreen = scene == "fullscreen"
    }
    #endif
}

private struct VideoSurface: UIViewRepresentable {
    let video: IRBVideoView
    let fullscreen: Bool
    func makeUIView(context: Context) -> IRBVideoView { video }
    func updateUIView(_ uiView: IRBVideoView, context: Context) {
        uiView.layer.cornerRadius = fullscreen ? 0 : 18
    }
}

@MainActor
private struct RemoteScreen: View {
    @ObservedObject var model: RemoteModel
    var body: some View {
        NavigationView {
            VStack(spacing: 0) {
                if !model.fullscreen {
                    if !model.connected { connectionPanel }
                    if model.connected && model.message != "已连接" {
                        Text(model.message).font(.caption).foregroundColor(.secondary)
                            .frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 16).padding(.top, 6)
                    }
                }
                mirror
                    .padding(.horizontal, model.fullscreen ? 0 : 12)
                    .padding(.vertical, model.fullscreen ? 0 : 8)
                if !model.fullscreen { controls }
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("RemoteBridge")
            .navigationBarTitleDisplayMode(.inline)
            .navigationBarHidden(model.fullscreen)
            .statusBar(hidden: model.fullscreen)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button { model.beginImport() } label: { Image(systemName: "square.and.arrow.down") }
                        .accessibilityLabel("导入配对").accessibilityIdentifier("remote.import")
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    HStack(spacing: 18) {
                        if model.connected {
                            Button { model.actions?.disconnect() } label: { Image(systemName: "power") }
                                .accessibilityLabel("断开连接")
                        }
                        Button { model.actions?.cancelInput(); model.showSettings = true } label: { Image(systemName: "slider.horizontal.3") }
                            .accessibilityLabel("显示与画质").accessibilityIdentifier("remote.settings")
                    }
                }
            }
            .fileImporter(isPresented: $model.showImporter, allowedContentTypes: [.json], allowsMultipleSelection: false) { result in
                switch result {
                case .success(let urls): if let url = urls.first { model.actions?.importProfile(url: url) }
                case .failure(let error):
                    if (error as NSError).code != NSUserCancelledError { model.message = error.localizedDescription }
                }
            }
            .sheet(isPresented: $model.showSettings) { DisplaySettings(model: model) }
            .sheet(isPresented: $model.showText) { TextComposer(model: model) }
        }
        .navigationViewStyle(.stack)
    }

    private var connectionPanel: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                Image(systemName: "iphone").font(.title2).foregroundColor(.accentColor)
                VStack(alignment: .leading, spacing: 3) {
                    Text("连接你的 iPhone").font(.headline)
                    Text(model.message).font(.caption).foregroundColor(.secondary).fixedSize(horizontal: false, vertical: true)
                }
            }
            HStack(spacing: 10) {
                TextField("设备 IP 地址", text: $model.host).keyboardType(.URL)
                    .textInputAutocapitalization(.never).disableAutocorrection(true)
                    .accessibilityIdentifier("remote.host")
                Divider().frame(height: 22)
                TextField("7443", text: $model.port).keyboardType(.numberPad)
                    .frame(width: 66).accessibilityIdentifier("remote.port")
            }
            .padding(12).background(Color(.tertiarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 10))
            .disabled(model.state != 0)
            Button { model.connect() } label: {
                HStack {
                    if model.state == 1 { ProgressView().tint(.white) }
                    Text(model.state == 1 ? "取消连接" : model.paired ? "连接设备" : "导入配对文件")
                        .fontWeight(.semibold)
                }.frame(maxWidth: .infinity).padding(.vertical, 5)
            }
            .buttonStyle(.borderedProminent).controlSize(.large).accessibilityIdentifier("remote.connect")
        }
        .padding(16).background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
        .padding(.horizontal, 12).padding(.top, 8)
    }

    private var mirror: some View {
        GeometryReader { geometry in
            ZStack {
                Color.black
                VideoSurface(video: model.video, fullscreen: model.fullscreen)
                    .accessibilityIdentifier("remote.video")
                if !model.connected {
                    VStack(spacing: 12) {
                        Image(systemName: "iphone").font(.system(size: 52, weight: .ultraLight))
                        Text(model.state == 1 ? "正在连接" : "连接后显示手机画面").font(.subheadline)
                    }.foregroundColor(.white.opacity(0.65)).allowsHitTesting(false)
                }
                VStack {
                    HStack(alignment: .top) {
                        if model.connected {
                            HStack(spacing: 8) {
                                Circle().fill(Color.green).frame(width: 5, height: 5)
                                Text(String(format: "%.0f FPS", model.decodedFPS))
                                Text(String(format: "%.1f ms", model.rtt)).foregroundColor(.white.opacity(0.7))
                                if model.vsync { Image(systemName: "waveform.path").accessibilityLabel("垂直同步已开启") }
                            }
                            .font(.system(size: 11, weight: .medium, design: .monospaced)).foregroundColor(.white)
                            .padding(.horizontal, 10).padding(.vertical, 8).background(.ultraThinMaterial, in: Capsule())
                        }
                        Spacer()
                        Button { model.toggleFullscreen() } label: {
                            Image(systemName: model.fullscreen ? "arrow.down.right.and.arrow.up.left" : "arrow.up.left.and.arrow.down.right")
                                .font(.system(size: 15, weight: .semibold)).frame(width: 40, height: 40)
                        }
                        .foregroundColor(.white).background(.ultraThinMaterial, in: Circle())
                        .accessibilityLabel(model.fullscreen ? "显示工具栏" : "全屏")
                        .accessibilityIdentifier("remote.fullscreen")
                    }
                    Spacer()
                }.padding(12)
                if model.connected && (model.locked || model.screenOff) {
                    VStack {
                        Spacer()
                        VStack(spacing: 10) {
                            Label(model.screenOff ? "屏幕已熄灭，连接保持中" : "设备已锁定", systemImage: "lock.fill")
                                .font(.subheadline.weight(.medium))
                            Text("在被控 iPhone 解锁后自动恢复控制").font(.caption).foregroundColor(.secondary)
                            if model.wakeSupported {
                                Button { model.perform("wake_screen") } label: { Label("亮屏", systemImage: "sun.max") }
                                    .buttonStyle(.borderedProminent).accessibilityIdentifier("remote.wake")
                            }
                        }.padding(16).background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
                    }.padding(20)
                }
                if model.canControl && model.floatingHome { FloatingHome(model: model, size: geometry.size) }
            }
            .clipShape(RoundedRectangle(cornerRadius: model.fullscreen ? 0 : 18))
            .environment(\.colorScheme, .dark)
        }
    }

    private var controls: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 4) {
                if model.wakeSupported { tool("亮屏", "sun.max") { model.perform("wake_screen") } }
                Group {
                    tool("多任务", "square.stack") { model.perform("app_switcher") }
                    tool("控制中心", "switch.2") { model.perform("control_center") }
                    tool("音量−", "speaker.minus") { model.perform("volume_down") }
                    tool("音量＋", "speaker.plus") { model.perform("volume_up") }
                    tool("文字", "keyboard") { model.openText() }
                }.disabled(!model.canControl)
                tool("旋转", "rotate.right") { model.actions?.rotate() }
            }.padding(.horizontal, 8)
        }
        .frame(height: 64).background(Color(.secondarySystemGroupedBackground)).disabled(!model.connected)
    }
    private func tool(_ title: String, _ symbol: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 5) { Image(systemName: symbol).font(.system(size: 20)); Text(title).font(.caption2) }
                .frame(minWidth: 62, minHeight: 50)
        }.buttonStyle(.plain).foregroundColor(model.connected ? .accentColor : .secondary)
    }
}

@MainActor
private struct FloatingHome: View {
    @ObservedObject var model: RemoteModel
    let size: CGSize
    @State private var offset = CGSize.zero
    @GestureState private var drag = CGSize.zero
    private func constrained(_ point: CGSize) -> CGSize {
        CGSize(width: min(0, max(-max(0, size.width - 80), point.width)), height: min(0, max(-max(0, size.height - 80), point.height)))
    }
    var body: some View {
        let position = constrained(CGSize(width: offset.width + drag.width, height: offset.height + drag.height))
        VStack { Spacer(); HStack { Spacer()
            Image(systemName: "house.fill").font(.system(size: 21, weight: .medium)).foregroundColor(.white)
                .frame(width: 56, height: 56).background(.ultraThinMaterial, in: Circle())
                .overlay(Circle().strokeBorder(.white.opacity(0.2), lineWidth: 1))
                .shadow(color: .black.opacity(0.2), radius: 6, y: 2).contentShape(Circle())
                .gesture(DragGesture(minimumDistance: 10)
                    .updating($drag) { value, current, _ in current = value.translation }
                    .onEnded { value in offset = constrained(CGSize(width: offset.width + value.translation.width, height: offset.height + value.translation.height)) }
                    .exclusively(before: TapGesture().onEnded { model.perform("press_home") }))
                .contextMenu {
                    Button { model.perform("press_home") } label: { Label("主屏幕", systemImage: "house") }
                    Button { model.perform("app_switcher") } label: { Label("多任务", systemImage: "square.stack") }
                    Button { model.perform("control_center") } label: { Label("控制中心", systemImage: "switch.2") }
                }
                .accessibilityLabel("虚拟 Home 键").accessibilityHint("轻点回主屏幕，长按显示更多操作，拖动可移动")
                .accessibilityAddTraits(.isButton).accessibilityAction { model.perform("press_home") }
                .accessibilityIdentifier("remote.home").offset(position)
        }}.padding(12)
    }
}

@MainActor
private struct DisplaySettings: View {
    @ObservedObject var model: RemoteModel
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationView {
            Form {
                Section(header: Text("视频")) {
                    Picker("目标帧率", selection: $model.fps) { Text("30 FPS").tag(30); Text("60 FPS").tag(60) }.pickerStyle(.segmented)
                    Picker("分辨率", selection: $model.scale) { Text("原生").tag(1.0); Text("一半").tag(0.5) }.pickerStyle(.segmented)
                }
                Section(footer: Text("开启时按屏幕刷新提交画面；关闭时解码完成即提交。")) {
                    Toggle(isOn: $model.vsync) { Label("垂直同步", systemImage: "waveform.path") }
                        .accessibilityIdentifier("remote.vsync")
                }
                Section(footer: Text("轻点回主屏幕，长按打开更多操作，拖动可移动。")) {
                    Toggle(isOn: $model.floatingHome) { Label("虚拟 Home 键", systemImage: "house") }
                }
                Section(header: Text("设备")) {
                    HStack { Text("地址"); Spacer(); Text(model.host.isEmpty ? "尚未配对" : model.host).foregroundColor(.secondary) }
                    HStack { Text("端口"); Spacer(); Text(model.port).foregroundColor(.secondary) }
                }
            }
            .navigationTitle("显示与画质").navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("完成") { dismiss() } } }
            .onChange(of: model.fps) { _ in model.preferencesChanged() }
            .onChange(of: model.scale) { _ in model.preferencesChanged() }
            .onChange(of: model.vsync) { _ in model.preferencesChanged() }
            .onChange(of: model.floatingHome) { UserDefaults.standard.set($0, forKey: "floatingHome") }
        }.navigationViewStyle(.stack)
    }
}

@MainActor
private struct TextComposer: View {
    @ObservedObject var model: RemoteModel
    @Environment(\.dismiss) private var dismiss
    @State private var text = ""
    @FocusState private var focused: Bool
    var body: some View {
        NavigationView {
            VStack(alignment: .leading, spacing: 12) {
                TextEditor(text: $text).keyboardType(.asciiCapable).textInputAutocapitalization(.never)
                    .disableAutocorrection(true).focused($focused).padding(6)
                    .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 12))
                Text("英文、数字及常用符号 · 最多 1,024 字符").font(.caption).foregroundColor(.secondary)
            }.padding(16).background(Color(.systemGroupedBackground))
            .navigationTitle("发送文字").navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("取消") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("发送") { model.actions?.send(text: text); dismiss() }.disabled(text.isEmpty || text.count > 1024 || !model.connected)
                }
            }
            .onAppear { focused = true }
        }.navigationViewStyle(.stack)
    }
}
