#!/bin/sh
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -eu

umask 022

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(tr -d '\r\n' < "$ROOT/VERSION")
NAME="xpad-installer-v$VERSION-windows-lockscreen-recovery"
OUTPUT=${1:-"$ROOT/dist/$NAME.zip"}
EXPECTED_SHA256=9f1ff6b7635548a11c57b2b8a31b0b98b941773bc6e0f2f00a5c3dc98e3a5fc0

command -v zip >/dev/null 2>&1 || {
  echo "zip is required" >&2
  exit 127
}

test -f "$ROOT/dist/xpad-install" || "$ROOT/scripts/build_single_elf.sh" "$ROOT/dist/xpad-install"
ACTUAL_SHA256=$(shasum -a 256 "$ROOT/dist/xpad-install" | awk '{print $1}')
if [ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]; then
  echo "xpad-install SHA-256 mismatch: expected $EXPECTED_SHA256, got $ACTUAL_SHA256" >&2
  exit 1
fi

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/xpad-installer-windows-recovery.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT HUP INT TERM
mkdir -p "$STAGE/$NAME"
install -m 0755 "$ROOT/dist/xpad-install" "$STAGE/$NAME/xpad-install"
install -m 0644 "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"

# cmd.exe and classic Notepad behave most consistently with CRLF files. The
# tracked source stays LF-only; only the generated Windows artifact is converted.
awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' \
  "$ROOT/windows/xpad2-lockscreen-recovery.bat" \
  > "$STAGE/$NAME/START-LOCKSCREEN-RECOVERY.bat"
awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' \
  "$ROOT/windows/README-LOCKSCREEN-RECOVERY.zh-CN.txt" \
  > "$STAGE/$NAME/README-FIRST.zh-CN.txt"

(
  cd "$STAGE/$NAME"
  shasum -a 256 \
    xpad-install \
    START-LOCKSCREEN-RECOVERY.bat \
    README-FIRST.zh-CN.txt \
    LICENSE \
    > SHA256SUMS
)

find "$STAGE/$NAME" -exec touch -t 200001010000 {} +
mkdir -p "$(dirname -- "$OUTPUT")"
rm -f "$OUTPUT"
(
  cd "$STAGE"
  COPYFILE_DISABLE=1 zip -q -X "$OUTPUT" \
    "$NAME/" \
    "$NAME/xpad-install" \
    "$NAME/START-LOCKSCREEN-RECOVERY.bat" \
    "$NAME/README-FIRST.zh-CN.txt" \
    "$NAME/LICENSE" \
    "$NAME/SHA256SUMS"
)

echo "Built Windows recovery archive: $OUTPUT"
