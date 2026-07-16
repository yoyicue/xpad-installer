# xpad-installer CLI

Standalone command-line installer for authorized XPad2 devices. The project
builds the arm64 Android executable `xpad-install`; it contains the installer
DEX in the ELF itself and does not require a separately copied DEX to execute
normal commands.

New to ADB? Start with the step-by-step
[Chinese beginner guide](docs/USAGE.zh-CN.md), which explains device
authorization, file transfer, every public command, exit codes, and common
errors.

This directory was extracted from `xpad2` revision `178782c`. It intentionally
contains the current, device-side implementation that was exercised on the
target firmware. The older host-side Python experiment, which depended on an
external CVE script at a machine-specific path, is not part of this project.

## Commands

```text
xpad-install self-test
xpad-install --version
xpad-install doctor
xpad-install install [--backend auto|provider|direct] APK
xpad-install upgrade [--backend auto|provider|direct] APK
xpad-install verify PACKAGE [VERSION_CODE]
xpad-install activate --starter=PATH --apk=MANAGER.apk
xpad-install autostart enable
xpad-install znxrun status
xpad-install znxrun ensure
xpad-install znxrun preflight
xpad-install znxrun create --package PACKAGE --apk UPDATE.apk [--apply]
xpad-install cleanup
```

Every `install` and `upgrade` verifies and, when needed, repairs the managed
0044 OEM installer identity, then performs the target APK transaction only
through that identity. The persistent source is a dedicated no-code, no-permission
package named `com.yoyicue.xpad2.installeranchor`, embedded in the ELF and not
used for normal application updates. If 0044 is missing or broken, the bounded
UID 1000/31317 runner is used only to repair 0044. The target APK is not handed
to 31317: repair must pass the outer 0044 health check before installation starts.
The v0.2.8 anchor uses versionCode 2 so a repair of already-persisted
attribution is a material PackageManager update, then polls full alias/UID/source
health for at most 60 seconds instead of treating an asynchronous `run-as` miss as
final. Progress is logged every five seconds. The version advance uses a full update; same-version repairs retain the
bounded inherit-existing transaction.
The 31317 repair implementation keeps the existing three-attempt policy, durably
saves and exactly restores the original hidden setting, records every phase and core PID under
`/data/local/tmp/.xpad-installer/logs`, and opens a per-boot circuit breaker if
Zygote, system_server, or SystemUI changes. Exit 75 then requires an ordinary
reboot before another 31317 attempt.

`--version`, `self-test`, `doctor`, `verify`, `cleanup`, and `znxrun status` are handled by
the native CLI before transport selection; none of them can acquire a 31317
runner. Unknown commands also fail before transport selection. `self-test` only
validates the locked ELF's embedded DEX and anchor.

`znxrun status` is read-only and reports `healthy`, `legacy`, `missing`, or
`invalid`. `znxrun ensure` is idempotent: it installs/verifies the signed anchor,
persists the exact installer attribution, restores the temporary OEM whitelist,
and accepts success only after both `dumpsys` attribution and the `run-as` UID
match the real `com.tal.pad.znxxservice` UID reported by PackageManager. That UID
is device-specific (for example 10070 or 10072), not a protocol constant. It
never edits `/data/system/packages.list` directly.

`activate` provisions BoomInstaller's local wireless-ADB boot path and starts
its Shizuku control plane directly as the current root or ADB-shell identity.
It never enters 0044 or 31317: those identities belong only to APK installation.
The normal installer path uses only managed 0044 for the target APK. The guarded
31317 transaction may repair 0044, but never becomes a target-APK fallback.

## Build

Requirements:

- JDK with `javac`
- Android SDK platform 35
- Android Build Tools 34 or a `D8` override
- Android NDK with an arm64 API 33 Clang toolchain

The defaults match the Homebrew Android toolchains used on the signing Mac.
All paths can be overridden with `ANDROID_SDK_ROOT`, `ANDROID_JAR`, `D8`,
`NDK_ROOT`, `NDK_HOST_TAG`, or `CC`.

```shell
make verify
```

The executable is written to `dist/xpad-install`. The build regenerates the
three DEX artifacts from the Java sources before linking the unified DEX and
the verified signed installer anchor into the ELF.

The repository contains the signed anchor used by ordinary reproducible builds.
Maintainers can rebuild and sign it with the protected XPad2 production key:

```shell
XPAD2_RELEASE_SIGNING_BACKUP=/path/to/signing-backup make carrier
```

The signing script pins the package, version, production certificate and RSA
recovery-key fingerprint. Private key material is never copied into the repo.

## Release package

```shell
make package
```

This produces `dist/xpad-installer-v0.2.8-android-arm64.zip`. The archive
contains the executable, this README, the Chinese beginner guide, the GPLv3
license, and a SHA-256 manifest for the executable.

## Windows lock-screen recovery package

Devices that ran `doctor` from v0.2.1 or older may have accidentally entered
the 31317 transport. The current release returns from native `doctor` before
transport selection. A bounded Windows recovery package for affected `/260`
devices can be built with:

```shell
make windows-recovery-package
```

This produces the separately frozen
`dist/xpad-installer-v0.2.2-windows-lockscreen-recovery.zip`. It contains the
locked v0.2.2 recovery ELF, a one-click batch file, a Chinese guide, the license, and
checksums. It intentionally does **not** contain `adb.exe`: users either extract
it into an existing Android Platform-Tools directory or provide `adb.exe` via
`PATH`.

The batch file captures pre/post recovery diagnostics, redacts the device
serial, runs only `self-test` and native `cleanup`, accepts exit 75 as requiring
the planned ordinary reboot, and opens Android's security settings after boot.
It never clears or creates a PIN, pattern, or password and never writes the raw
`lockscreen.disabled` secure setting.

## Windows safe-install GUI

The separate beginner-facing Windows toolkit installs a selected APK through
the same locked `xpad-install` v0.2.8 engine:

```shell
make windows-toolkit-package
```

This produces `dist/xpad-safe-install-toolkit-v2.8.0.zip`. The archive includes
`xpad-safe-install-gui.bat`, the Python GUI, the locked device-side executable,
the Chinese guide, license, and checksums. It does not bundle `adb.exe`; Android
Platform-Tools may be placed beside the batch file, in a `platform-tools`
subdirectory, or on `PATH`.

The GUI enters installation immediately after a successful native `doctor`,
uses one managed `auto` transaction, and runs cleanup on both success and
failure. It distinguishes an APK permission-owner conflict from the exit-75
boot safety breaker, so an ordinary APK defect is no longer reported as a
mandatory reboot.

## Deploy and use

```shell
adb -s SERIAL push dist/xpad-install /data/local/tmp/xpad-install
adb -s SERIAL shell chmod 755 /data/local/tmp/xpad-install
adb -s SERIAL shell /data/local/tmp/xpad-install doctor
adb -s SERIAL shell \
  /data/local/tmp/xpad-install install --backend auto /sdcard/Download/app.apk
```

To activate an installed BoomInstaller build:

```shell
adb -s SERIAL shell '
APK=$(pm path com.yoyicue.boominstaller)
APK=${APK#package:}
STARTER=${APK%/base.apk}/lib/arm64/libshizuku.so
/data/local/tmp/xpad-install activate --starter="$STARTER" --apk="$APK"
'
```

The command-line bootstrap binary itself is normally placed in
`/data/local/tmp`. BoomInstaller's later boot/runtime path uses the native
library packaged in its APK and does not depend on this directory.

## Project layout

- `native/`: single-file Android CLI and embedded-DEX assembly
- `exploit/`: bounded Java installer operations and generated DEX files
- `carrier/`: minimal installer-anchor manifest and verified signed APK
- `docs/`: end-user documentation
- `scripts/`: reproducible DEX and ELF build scripts
- `windows-toolkit/`: beginner-facing Windows safe-install GUI and batch launcher
- `tools/`: release packaging for host-side toolkits
- `tests/`: standalone source/package integrity checks

This tool is firmware-specific. Do not run it against unrelated devices.

## License

Copyright (C) 2026 yoyicue.

This project is licensed under the GNU General Public License version 3 only
(`GPL-3.0-only`). See [LICENSE](LICENSE).
