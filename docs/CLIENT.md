# Windows receiver

Download `windows-client-x64` from a successful build-client Actions run and
extract the entire ZIP. Keep the DLLs and licenses alongside iOSRemoteBridge.exe.
The source artifact in the same run contains the corresponding FFmpeg sources.

1. Generate pairing files with `python tools/provision.py --host IPHONE_WIFI_IP`.
2. Follow DEVICE_TESTING.md to provision the iPhone over SSH and start the daemon.
3. Open iOSRemoteBridge.exe, choose client-config.json, then Connect.
4. Approve the request physically on the unlocked iPhone. The red STOP control
   stays visible on the iPhone during the session. Every reconnect asks again.
5. Drag with the mouse for touch input. Two clicks produce a double tap. Hold
   the mouse button for a long press. Home, App switcher and volume have buttons.
6. Use Send text for US-layout ASCII text; the iPhone asks for separate approval.
   Arbitrary Unicode text is not implemented. Raw HID keys are exposed through
   the automation API and also require local confirmation.

Keyboard shortcuts: F1 rotates clockwise; F2 requests 30 FPS; F3 requests 60;
F4 requests half resolution; F5 requests native resolution; Ctrl+T focuses the
text field; Esc disconnects. Enter in the kbps field changes the bitrate.
Quality changes are applied at a frame boundary. A second simultaneous quality
change can return a busy error; retry after the first completes.

Statistics show decoded FPS, **network round-trip time**, last decode time and
the selected decoder. RTT is not end-to-end display latency. D3D11VA is attempted
when supported, with software fallback. The initial renderer copies decoded
pixels to system memory for GDI presentation. This is not a zero-copy GPU renderer.
The title reports software if GPU decoding was unavailable or reset.

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
cmake -S . -B build -A x64 -DIRB_BUILD_CLIENT=ON -DCMAKE_TOOLCHAIN_FILE=PATH_TO_VCPKG/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Internet access is not configured. Use a separately secured VPN if required;
the prototype listener is Wi-Fi constrained and has no NAT traversal. Native
macOS desktop playback and browser playback are not implemented.
