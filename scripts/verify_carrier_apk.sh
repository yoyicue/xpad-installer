#!/usr/bin/env bash
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
APK=${1:-"$ROOT/carrier/xpad2-installer-anchor.apk"}
EXPECTED_PACKAGE=com.yoyicue.xpad2.installeranchor
EXPECTED_CERT_SHA256=3cb5b69579d23197ced8100818a85a46b821383a504b394a44cfe3e98ade78a2

find_tool() {
  local name=$1 candidate root
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return
  fi
  for root in "${ANDROID_SDK_ROOT:-}" /opt/homebrew/share/android-commandlinetools \
    "$HOME/Library/Android/sdk"; do
    [[ -n "$root" && -d "$root" ]] || continue
    candidate=$(find "$root" -type f -name "$name" 2>/dev/null | sort -V | tail -1)
    if [[ -n "$candidate" && -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return
    fi
  done
  printf 'missing Android build tool: %s\n' "$name" >&2
  exit 127
}

[[ -f "$APK" ]] || {
  printf 'missing signed installer anchor: %s\n' "$APK" >&2
  exit 1
}
AAPT2=$(find_tool aapt2)
APKSIGNER=$(find_tool apksigner)
badging=$("$AAPT2" dump badging "$APK")
[[ "$badging" == *"package: name='$EXPECTED_PACKAGE' versionCode='1'"* ]]
[[ "$badging" == *"application-label:'XPad2 Installer Anchor'"* ]]
permissions=$("$AAPT2" dump permissions "$APK")
[[ "$permissions" != *"uses-permission:"* ]]
verification=$("$APKSIGNER" verify --verbose --print-certs "$APK")
[[ "$verification" == *"Verified using v1 scheme (JAR signing): false"* ]]
[[ "$verification" == *"Verified using v2 scheme (APK Signature Scheme v2): true"* ]]
[[ "$verification" == *"Verified using v3 scheme (APK Signature Scheme v3): true"* ]]
actual_cert=$(sed -n 's/^Signer #1 certificate SHA-256 digest: //p' <<<"$verification")
[[ "$actual_cert" == "$EXPECTED_CERT_SHA256" ]]
printf 'XPAD2_ANCHOR_VERIFY_OK package=%s versionCode=1 cert_sha256=%s\n' \
  "$EXPECTED_PACKAGE" "$actual_cert"
