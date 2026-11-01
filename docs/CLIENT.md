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
Zero disables it. The filter runs after resizing, uses a small luma unsharp mask
and limits halos. Flat colors and channel differences are preserved; the control
does not change exposure, gamma, encoder quality or phone settings. The selected
strength lasts for the current viewer process. Decoding hands off immutable
frames so sharpening does not hold the decoder's frame mutex.

Statistics show decoded FPS, **network round-trip time**, last decode time and
the selected decoder. RTT is not end-to-end display latency. D3D11VA is attempted
when supported, with software fallback. The initial renderer copies decoded
pixels to system memory for GDI presentation. Each paint is composed in a reused
offscreen bitmap and copied to the window in one pass; background erasure does
not clear the displayed image between frames. This is not a zero-copy GPU renderer.
The title reports software if GPU decoding was unavailable or reset.

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
