# Windows receiver

Download `windows-client-x64` from a successful build-client Actions run and
extract the entire ZIP. Keep the DLLs and licenses alongside iOSRemoteBridge.exe.
The source artifact in the same run contains the corresponding FFmpeg sources.
Windows also needs Microsoft's x64 Visual C++ v14 runtime. If it is missing,
install the [current supported redistributable from Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
The build uses the dynamic runtime; proprietary redistributable binaries are
not silently copied from the build machine into this artifact.

1. Generate pairing files with `python tools/provision.py --host IPHONE_WIFI_IP`.
2. Follow DEVICE_TESTING.md to provision the iPhone over SSH and start the daemon.
3. Open iOSRemoteBridge.exe, choose client-config.json, then Connect.
4. Approve the request physically on the unlocked iPhone. No floating control
   button covers the screen. Lock the phone, click Disconnect or press Esc
   to end the session. Every reconnect asks again.
5. Drag with the mouse for touch input. Two clicks produce a double tap. Hold
   the mouse button for a long press. Home, App switcher and volume have buttons.
6. Use Send text for US-layout ASCII text; the iPhone asks for separate approval.
   Arbitrary Unicode text is not implemented. Raw HID keys are exposed through
   the automation API and also require local confirmation.

To open a paired viewer and start connecting immediately, use
`iOSRemoteBridge.exe PATH_TO_CLIENT_CONFIG_JSON --connect`. Local iPhone approval
is still required. Omit `--connect` to load the pairing path without connecting.

The interactive viewer requests 60 FPS and a three-frame window by default.
The capture/encode/network path still determines the achieved decoded FPS;
selecting 60 does not establish that 60 frames were captured or displayed.

Keyboard shortcuts: F1 rotates clockwise; F2 requests 30 FPS; F3 requests 60;
F4 requests half resolution; F5 requests native resolution; F6 fits the window
to the device screen on the current monitor; F7/F8 decrease/increase sharpening
by 10 percentage points; Ctrl+T focuses the
text field; Esc disconnects. Enter in the kbps field changes the bitrate.
Quality changes are applied at a frame boundary. A second simultaneous quality
change can return a busy error; retry after the first completes.

The viewer opens with its video area fitted to the device's aspect ratio,
including after Rotate/F1. Keep ratio constrains dragging any window edge or
corner to those proportions. Uncheck it to resize freely; the image itself
remains proportional with letterboxing. Fit/F6 fits the image within the current
monitor's work area, with room for the toolbar and window borders. On a small
display the minimum toolbar width can require a little letterboxing. The
toolbar reflows into compact rows so controls stay visible in portrait mode.

Hide tools (F9) hides the entire toolbar, including the pairing and quality
controls, so the video uses the full client area. F9 restores it. You can also
right-click the video or the window title bar and choose Show toolbar. Toggling
does not reconnect or reset control values. Keep ratio/Fit account for the
current toolbar height, and mouse coordinates follow the displayed image.

The Sharp slider adjusts local edge sharpening from 0–100%, starting at 40%.
Zero disables it. GPU presentation uses AMD RCAS after scaling; the GDI fallback
uses a luma unsharp mask with bounded halos. Flat colors are preserved; the control
does not change exposure, gamma, encoder quality or phone settings. The selected
strength lasts for the current viewer process. Decoding hands off immutable
frames so sharpening does not hold the decoder's frame mutex.

VSync is on by default. D3D11 flip-model presentation uses sync interval 1;
turning it off uses interval 0 with tearing allowed when supported. Refresh rate
and incoming frames still limit visible FPS. VSync can add presentation latency.

FSR 1 enables AMD's EASU spatial upscaler when the displayed image is larger
than the decoded frame. It is off by default. For example, F4 requests
half-resolution video, which FSR 1 can upscale to the viewer size. Native-size
or smaller images use regular sampling. It does not generate extra frames.

FSR 4/3/2.3 (exp.) is a separate, **off-by-default experiment**. AMD SDK 2.4
enumerates providers and attempts 4, then 3, then 2.3. The display notice reports
the actual context's version. This video stream has no engine depth, motion or
camera jitter: the experiment supplies a flat plane, zero motion, fixed exposure
and resets history on every frame. It has no temporal accumulation and is not a
complete game-style FSR integration. It uses DX12 conversion/upload/readback
before D3D11 presentation and can reduce FPS or add latency. The stable renderer
continues if no provider works. Enlarge the image or request F4 to exercise it.

With Keyboard+mouse off, H opens Home, C opens Control Center, + / - change
volume, and Up / Down perform upward/downward swipes. Local pairing, text and
bitrate fields, and the sharpness slider, keep their normal keyboard behavior.
Mouse wheel scrolling is available over the video.

Keyboard+mouse / F10 requests separate approval on the iPhone for live HID key
down/up events for this connection. After Allow, focus the video to type; H/C
then type characters instead of navigation shortcuts. Mouse clicks/drags remain
touches, the wheel scrolls, and right-click performs a long press. F1–F10 and Esc
remain local controls. F9 or the title-bar menu restores hidden tools. Switching
focus releases held keys. Turning the mode off, locking, or disconnecting revokes
the grant; reconnecting requires approval again. This is US-layout HID input,
without host IME composition/Unicode text transfer or a native iOS mouse pointer.

Statistics show decoded FPS, **network round-trip time**, last decode time and
the selected decoder. RTT is not end-to-end display latency. D3D11VA is attempted
when supported, with software fallback. Decoded pixels still pass through system
memory, then upload to D3D11; this is not a zero-copy decoder/presenter. A video
child window separates presentation from the toolbar. Advanced-renderer failure
falls back to buffered GDI and disables VSync/FSR controls. The title reports
software if GPU decoding was unavailable or reset. `irb-gpu-check.exe` checks
native range, EASU/RCAS output and presentation; `--warp` tests software D3D.
`irb-fsr-check.exe --probe` lists SDK versions; without the flag it checks a
synthetic single-frame upscale and reports conversion/GPU/readback time.

GPU download retains the decoded frame's color properties. Full-range and
limited-range video use the corresponding conversion into full-range BGRA,
with the signaled YCbCr matrix. This corrects highlight clipping caused by
interpreting full-range phone video as limited-range NV12. The diagnostic
`irb-color-check.exe diagnostics/gray-full.h264 diagnostics/gray-limited.h264`
checks both ranges using software and the available hardware decoder.

Rotation is explicitly selected by the viewer. Input maps through the inverse
rotation and letterboxing into the natural portrait digitizer plane. Automatic
foreground-app orientation tracking is not yet implemented. Multi-touch is
available through the API; this mouse UI controls one contact.

The client reconnects with bounded backoff, discards queued input on disconnect,
and never replays a gesture. A certificate pin mismatch must be investigated;
replace pairing files only through the trusted provisioning channel. Protect
client-config.json with your Windows account ACL and do not put it in shared
folders. No credentials are saved by the GUI.

Build from source on Windows with Visual Studio 2022 C++ Build Tools, CMake and
the pinned vcpkg checkout:

```powershell
python tools/prepare_ffmpeg_port.py PATH_TO_VCPKG .vcpkg-overlays
cmake -S . -B build -A x64 -DIRB_BUILD_CLIENT=ON -DCMAKE_TOOLCHAIN_FILE=PATH_TO_VCPKG/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_OVERLAY_PORTS=.vcpkg-overlays
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Internet access is not configured. Use a separately secured VPN if required;
the prototype listener is Wi-Fi constrained and has no NAT traversal. Native
macOS desktop playback and browser playback are not implemented.
