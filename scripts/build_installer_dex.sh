#!/bin/sh
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK_ROOT=${ANDROID_SDK_ROOT:-/opt/homebrew/share/android-commandlinetools}
ANDROID_JAR=${ANDROID_JAR:-$SDK_ROOT/platforms/android-35/android.jar}
D8=${D8:-$SDK_ROOT/build-tools/34.0.0/d8}
WORK=${TMPDIR:-/tmp}/xpad-installer-build

rm -rf "$WORK"
mkdir -p "$WORK/direct" "$WORK/direct-dex" "$WORK/provider" "$WORK/provider-dex"

javac -cp "$ANDROID_JAR" -d "$WORK/direct" "$ROOT/exploit/DirectInstaller.java"
"$D8" --min-api 33 --output "$WORK/direct-dex" \
  "$WORK/direct/DirectInstaller.class" "$WORK/direct/DirectInstaller\$1.class"
cp "$WORK/direct-dex/classes.dex" "$ROOT/exploit/installer.dex"

javac -cp "$ANDROID_JAR" -d "$WORK/provider" "$ROOT/exploit/ZnxxProviderInstaller.java"
"$D8" --min-api 33 --output "$WORK/provider-dex" "$WORK/provider/ZnxxProviderInstaller.class"
cp "$WORK/provider-dex/classes.dex" "$ROOT/exploit/znxx_provider_installer.dex"

mkdir -p "$WORK/unified" "$WORK/unified-dex"
javac -cp "$ANDROID_JAR" -d "$WORK/unified" \
  "$ROOT/exploit/DirectInstaller.java" \
  "$ROOT/exploit/ZnxxProviderInstaller.java" \
  "$ROOT/exploit/Znxrun0044.java" \
  "$ROOT/exploit/XpadInstaller.java"
"$D8" --min-api 33 --output "$WORK/unified-dex" "$WORK/unified"/*.class
cp "$WORK/unified-dex/classes.dex" "$ROOT/exploit/xpad_installer.dex"

echo "Built Direct, Provider, and unified installer dex files"
