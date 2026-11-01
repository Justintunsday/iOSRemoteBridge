# First installation and physical verification

No device has been tested by this project yet. Use the owner's iPhone SE 2
(iPhone12,8) on iOS 16.1.1 with an active Dopamine rootless jailbreak and ElleKit.
Keep working SSH access available for recovery. A reboot requires jailbreak
reactivation; the service is not available in the stock pre-jailbreak state.

## Prepare and install

1. Download `ios-rootless-deb` from a successful build-ios Actions run. Inspect
   the run's SDK, architecture, package-content and signing logs. The daemon is
   arm64; the injected library includes arm64e for the A13 SpringBoard ABI.
2. On Windows, create pairing files (Python + OpenSSL are needed on the provisioning
   computer; OpenSSL bundled with Git for Windows is detected):

   ```powershell
   python tools/provision.py --host 192.168.1.20 --output credentials/pairing
   scp PATH_TO_PACKAGE.deb mobile@192.168.1.20:/var/mobile/irb.deb
   scp credentials/pairing/device-config.json credentials/pairing/server.p12 mobile@192.168.1.20:/var/mobile/
   ```

3. SSH to the iPhone and run these commands with the owner's administrative
   access. The address and package path above are examples, not an identified
   device from this development session:

   ```sh
   sudo dpkg -i /var/mobile/irb.deb
   sudo install -d -o mobile -g mobile -m 700 /var/jb/var/mobile/Library/iOSRemoteBridge
   sudo install -o mobile -g mobile -m 600 /var/mobile/device-config.json /var/jb/var/mobile/Library/iOSRemoteBridge/device-config.json
   sudo install -o mobile -g mobile -m 600 /var/mobile/server.p12 /var/jb/var/mobile/Library/iOSRemoteBridge/server.p12
   rm /var/mobile/device-config.json /var/mobile/server.p12
   sudo launchctl bootstrap system /var/jb/Library/LaunchDaemons/io.github.justintunsday.iosremotebridge.plist
   ```

4. Respring once through the jailbreak's normal UI to load IRBSpringBoard.dylib.
   Keep the iPhone unlocked and touch its screen once so the HID observer can
   learn the actual sender ID. If launchctl reports an already-loaded service,
   inspect `launchctl print system/io.github.justintunsday.iosremotebridge`, then
   use the restart procedure below. Do not repeatedly bootstrap a failing job.
5. Inspect `/var/jb/var/mobile/Library/iOSRemoteBridge/daemon.log`. A ready TLS
   listener still does not prove the bridge, capture or HID is working.

## Prove consent and capture before input

Connect with the Windows client or `python -m automation --config
credentials/pairing/client-config.json get_device_info`. First reject the local
prompt and verify that no video is transmitted. Reconnect after the rejection
cooldown (up to 30 seconds), approve viewing, and check that the red STOP control
is visible and usable. Verify black/protected content stays protected. Do not
disable content protections to make a black capture appear.

Check get_device_info reports the correct model and iOS version. Confirm native
mode is 750x1334 in the natural portrait capture plane; rotate the Windows view
to 1334x750 for landscape. Show a numbered grid to check every edge and pixel
scale, then switch Home Screen, Settings and another app repeatedly. Compare a
saved JPEG to the live device. A non-null surface or a 60-Hz timer is insufficient.

If CARenderServer returns an error, black output or wrong geometry, record the
exact OS build, error, dimensions and SpringBoard logs. This backend's behavior
on the target is a validation gate. No screenshot fallback or other private API
should be silently substituted. A ReplayKit Broadcast Upload Extension is a
possible separate future backend, requiring an owner-started broadcast.

## Prove input and recovery

Request control explicitly (`--control` for Python) and approve locally. Use a
harmless test screen. Check tap, two taps, long press, a timed swipe, all corners,
Home and App Switcher. Rotate the viewer through all four rotations and verify
letterbox clicks are ignored. Test contacts 0 and 1 independently through the
touch API, then together. Verify a network disconnect releases both contacts.
HID delivery, sender discovery, volume and the double-Home App Switcher method
are experimental until these checks pass on iOS 16.1.1.

For ASCII text, focus a test field and send an entire sentence with mixed case,
digits, punctuation and a newline. Approve the separate prompt physically. A
key operation sends a single press/release pair, not an indefinitely held key.
Unicode input returns an error. Verify remote touches cannot approve a pending
sensitive-action prompt. Launch a harmless installed app by bundle ID and record
whether the dynamically resolved workspace API succeeds.

Tap STOP during a drag, a long press and text entry. Verify immediate input
cancellation, no further video and a fresh local approval after reconnect.
Repeat for device lock, Wi-Fi loss, daemon restart and respring. Keep the display
awake manually for the first performance tests: the implementation does not
disable auto-lock, wake a locked phone, bypass a passcode or run while stock iOS
is booted before jailbreak reactivation. Follow PERFORMANCE.md for 30/60-FPS,
latency, memory, thermal, power and soak-test measurements.

## Stop, start, recover and uninstall

Run on the iPhone with administrative access:

```sh
# Stop immediately. Capture/input stop when the bridge loses its daemon.
sudo launchctl bootout system/io.github.justintunsday.iosremotebridge

# Start after provisioning, or after a deliberate stop.
sudo launchctl bootstrap system /var/jb/Library/LaunchDaemons/io.github.justintunsday.iosremotebridge.plist

# Restart an already registered job after a recoverable failure.
sudo launchctl kickstart -k system/io.github.justintunsday.iosremotebridge

# Uninstall the package, then respring normally to unload the dylib.
sudo dpkg -r io.github.justintunsday.iosremotebridge
```

If SpringBoard becomes unstable, bootout the daemon and rename
`/var/jb/Library/MobileSubstrate/DynamicLibraries/IRBSpringBoard.dylib` to
`IRBSpringBoard.dylib.disabled` over SSH, then respring normally. A fresh package
installation restores the file. No kernel modification is used.

Uninstallation intentionally preserves pairing state for review. After the
package is removed and the dylib unloaded, delete the explicitly named
`/var/jb/var/mobile/Library/iOSRemoteBridge` directory if the owner wants all
tokens, identities and logs removed. Also remove the local client pairing folder.
Never publish those files or include them in diagnostic archives.

Record results in a dated device-validation report: model, exact OS build,
jailbreak/ElleKit versions, package commit, each test outcome and log evidence.
Until that report exists, compatibility and performance remain unverified.
