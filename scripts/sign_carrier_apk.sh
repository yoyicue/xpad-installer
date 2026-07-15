#!/usr/bin/env bash
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail
umask 077

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BACKUP=${XPAD2_RELEASE_SIGNING_BACKUP:-}
OUTPUT=${1:-"$ROOT/carrier/xpad2-installer-anchor.apk"}
EXPECTED_PACKAGE=com.yoyicue.xpad2.installeranchor
EXPECTED_CERT_SHA256=3cb5b69579d23197ced8100818a85a46b821383a504b394a44cfe3e98ade78a2
EXPECTED_RSA_FINGERPRINT=SHA256:cOVa4bIB0vgNbqR5Vi95Q0QFDLY7lJX79sHEHTm1Q2U
KEY_ALIAS=boom-xpad2-release

die() {
  printf 'XPAD2_ANCHOR_SIGN_REFUSED reason=%s\n' "$1" >&2
  exit 1
}

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
  die "$name-missing"
}

[[ -n "$BACKUP" ]] || die signing-backup-not-set
[[ -f "$BACKUP/SHA256SUMS" ]] || die backup-manifest-missing
(
  cd "$BACKUP"
  shasum -a 256 -c SHA256SUMS >/dev/null
) || die backup-checksum

KEYSTORE="$BACKUP/xpad2-boom-release.p12"
SECRET_FILE="$BACKUP/xpad2-boom-release-password.rsa-oaep-sha256"
RECOVERY_KEY="$BACKUP/recovery-rsa/id_rsa"
CERT="$BACKUP/xpad2-boom-release-cert.pem"
[[ -f "$KEYSTORE" && -f "$SECRET_FILE" && -f "$RECOVERY_KEY" && -f "$CERT" ]] ||
  die signing-material-missing

cert_sha=$(openssl x509 -in "$CERT" -outform DER | shasum -a 256 | awk '{print $1}')
[[ "$cert_sha" == "$EXPECTED_CERT_SHA256" ]] || die certificate-mismatch
fingerprint=$(ssh-keygen -lf "$BACKUP/recovery-rsa/id_rsa.pub" | awk '{print $2}')
[[ "$fingerprint" == "$EXPECTED_RSA_FINGERPRINT" ]] || die recovery-key-mismatch

APKSIGNER=$(find_tool apksigner)
AAPT2=$(find_tool aapt2)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/xpad2-anchor-sign.XXXXXX")
trap 'rm -rf "$WORK"; unset XPAD2_ANCHOR_PASSWORD' EXIT HUP INT TERM

"$ROOT/scripts/build_carrier_apk.sh" "$WORK/unsigned.apk"
cp "$RECOVERY_KEY" "$WORK/recovery-key.pem"
chmod 600 "$WORK/recovery-key.pem"
ssh-keygen -p -m PEM -P '' -N '' -f "$WORK/recovery-key.pem" >/dev/null
XPAD2_ANCHOR_PASSWORD=$(openssl pkeyutl -decrypt \
  -inkey "$WORK/recovery-key.pem" \
  -pkeyopt rsa_padding_mode:oaep \
  -pkeyopt rsa_oaep_md:sha256 \
  -in "$SECRET_FILE")
export XPAD2_ANCHOR_PASSWORD

mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"
"$APKSIGNER" sign \
  --ks "$KEYSTORE" \
  --ks-type PKCS12 \
  --ks-key-alias "$KEY_ALIAS" \
  --ks-pass env:XPAD2_ANCHOR_PASSWORD \
  --key-pass env:XPAD2_ANCHOR_PASSWORD \
  --v1-signing-enabled false \
  --v2-signing-enabled true \
  --v3-signing-enabled true \
  --v4-signing-enabled false \
  --out "$OUTPUT" \
  "$WORK/unsigned.apk"

badging=$("$AAPT2" dump badging "$OUTPUT")
[[ "$badging" == *"package: name='$EXPECTED_PACKAGE' versionCode='1'"* ]] ||
  die signed-package-mismatch
verification=$("$APKSIGNER" verify --verbose --print-certs "$OUTPUT")
[[ "$verification" == *"Verified using v2 scheme (APK Signature Scheme v2): true"* ]] ||
  die v2-signature
[[ "$verification" == *"Verified using v3 scheme (APK Signature Scheme v3): true"* ]] ||
  die v3-signature
actual_cert=$(sed -n 's/^Signer #1 certificate SHA-256 digest: //p' <<<"$verification")
[[ "$actual_cert" == "$EXPECTED_CERT_SHA256" ]] || die signer-mismatch

unset XPAD2_ANCHOR_PASSWORD
printf 'XPAD2_ANCHOR_SIGN_OK cert_sha256=%s output=%s\n' "$actual_cert" "$OUTPUT"
