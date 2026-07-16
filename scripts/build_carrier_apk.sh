#!/usr/bin/env bash
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUTPUT=${1:-"$ROOT/dist/xpad2-installer-anchor-unsigned.apk"}
SDK_ROOT=${ANDROID_SDK_ROOT:-/opt/homebrew/share/android-commandlinetools}
ANDROID_JAR=${ANDROID_JAR:-$SDK_ROOT/platforms/android-35/android.jar}

find_tool() {
  local name=$1 candidate
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return
  fi
  candidate=$(find "$SDK_ROOT/build-tools" -type f -name "$name" 2>/dev/null |
    sort -V | tail -1)
  [[ -n "$candidate" && -x "$candidate" ]] || {
    printf 'missing Android build tool: %s\n' "$name" >&2
    exit 127
  }
  printf '%s\n' "$candidate"
}

[[ -f "$ANDROID_JAR" ]] || {
  printf 'missing Android platform jar: %s\n' "$ANDROID_JAR" >&2
  exit 127
}

AAPT2=$(find_tool aapt2)
ZIPALIGN=$(find_tool zipalign)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/xpad2-installer-anchor.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

"$AAPT2" link \
  -I "$ANDROID_JAR" \
  --manifest "$ROOT/carrier/AndroidManifest.xml" \
  --min-sdk-version 24 \
  --target-sdk-version 35 \
  --version-code 2 \
  --version-name 2.0 \
  -o "$WORK/unaligned.apk"

mkdir -p "$(dirname "$OUTPUT")"
"$ZIPALIGN" -f 4 "$WORK/unaligned.apk" "$OUTPUT"
printf 'Built unsigned installer anchor: %s\n' "$OUTPUT"
