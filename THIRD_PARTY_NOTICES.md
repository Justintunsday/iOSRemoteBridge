# Third-party software and research

Original iOSRemoteBridge code is MIT; see LICENSE. The capture, IOHID and
VideoToolbox implementation was informed by e1abrador/ScreenMirror at
`d8b801e9805e9946a96af57c8a99d65831d76854`. Its MIT notice is retained in
[licenses/ScreenMirror-MIT.txt](licenses/ScreenMirror-MIT.txt), including in the
installed iOS package. The new protocol is not compatible with ScreenMirror.

ZXTouch Rootless (`a602da112ef1a688fb2601424443abb1ccc28c63`) was inspected as a
technical reference. It is GPL-3.0. No ZXTouch source, objects or binaries are
linked or distributed here. Any future integration must be a separately
licensed project or an explicit external adapter. WebDriverAgent
(`3d6a4e03cdd673e540289c610f7e189f8ffea434`) is BSD-3-Clause; researched only,
not bundled. Its XCTest runner is not required for this prototype.

Windows uses dynamically linked FFmpeg `avcodec`, `avutil` and `swscale`, built
without the `gpl`, `version3` or `nonfree` vcpkg features. FFmpeg is LGPL-2.1-or-later
for this configuration. Source and build patches are in the
`windows-ffmpeg-corresponding-source` artifact alongside the binary artifact.
Users may replace those DLLs with compatible versions. No restriction on
reverse engineering for debugging modifications to these libraries is imposed.
The executable artifact includes the full dependency license texts.
The generated overlay enables only the H.264 decoder/parser and its D3D11VA
accelerators, with no video encoders or unrelated codecs. Its exact portfile,
patches, generator and compiled configuration accompany the source archive.

Dependencies are resolved by vcpkg baseline
`9a5dfef5d1a0438a7d24856a85bd60f309d355e9`. See vcpkg.json and the workflow
logs for exact versions and build flags. Rebuild with that vcpkg commit and
`x64-windows` (dynamic linking), not a static triplet. Relevant sources:

- FFmpeg: https://github.com/FFmpeg/FFmpeg and https://ffmpeg.org/legal.html
- OpenSSL: https://github.com/openssl/openssl (Apache-2.0 for OpenSSL 3)
- nlohmann/json: https://github.com/nlohmann/json (MIT)
- vcpkg ports/patches: https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports
- Theos: https://github.com/theos/theos (build tooling, not redistributed)
- Optional MCP Python SDK: https://github.com/modelcontextprotocol/python-sdk/tree/v2.3.0 (MIT; installed separately through pip)

The FFmpeg command installed by Chocolatey in CI only generates a test fixture.
It is not copied into the Windows client artifact or linked to this application.
Apple SDKs/frameworks are subject to Apple's terms and are not redistributed.
