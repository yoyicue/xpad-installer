# xpad-installer CLI

Standalone command-line installer for authorized XPad2 devices. The project
builds the arm64 Android executable `xpad-install`; it contains the installer
DEX in the ELF itself and does not require a separately copied DEX to execute
normal commands.

This directory was extracted from `xpad2` revision `178782c`. It intentionally
contains the current, device-side implementation that was exercised on the
target firmware. The older host-side Python experiment, which depended on an
external CVE script at a machine-specific path, is not part of this project.

## Commands

```text
xpad-install doctor
xpad-install install [--backend auto|provider|direct] APK
xpad-install upgrade [--backend auto|provider|direct] APK
xpad-install verify PACKAGE [VERSION_CODE]
xpad-install activate --starter=PATH --apk=MANAGER.apk
xpad-install autostart enable
xpad-install znxrun preflight
xpad-install znxrun create --apk UPDATE.apk [--apply]
xpad-install cleanup
```

`auto` first tries the firmware's UID 10072/0044 OEM installer identity. When
that route is unavailable, it uses the bounded UID 1000/31317 runner. The
31317 implementation aligns both Zygotes, restores the hidden setting, stops
the sacrificial activities, and removes transfer artifacts before returning.

`activate` starts BoomInstaller using UID 10072, root, or UID 1000 as
available. `autostart enable` provisions BoomInstaller's local wireless-ADB
boot path.

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
three DEX artifacts from the Java sources before linking the unified DEX into
the ELF.

## Release package

```shell
make package
```

This produces `dist/xpad-installer-v0.1.0-android-arm64.zip`. The archive
contains the executable, this README, the GPLv3 license, and a SHA-256 manifest
for the executable.

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
- `scripts/`: reproducible DEX and ELF build scripts
- `tests/`: standalone source/package integrity checks

This tool is firmware-specific. Do not run it against unrelated devices.

## License

Copyright (C) 2026 yoyicue.

This project is licensed under the GNU General Public License version 3 only
(`GPL-3.0-only`). See [LICENSE](LICENSE).
