#!/bin/sh
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -eu

umask 022

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(tr -d '\r\n' < "$ROOT/VERSION")
NAME="xpad-installer-v$VERSION-android-arm64"
OUTPUT=${1:-"$ROOT/dist/$NAME.zip"}

command -v zip >/dev/null 2>&1 || {
  echo "zip is required" >&2
  exit 127
}

"$ROOT/scripts/build_single_elf.sh" "$ROOT/dist/xpad-install"

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/xpad-installer-release.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT HUP INT TERM
mkdir -p "$STAGE/$NAME"
install -m 0755 "$ROOT/dist/xpad-install" "$STAGE/$NAME/xpad-install"
install -m 0644 "$ROOT/README.md" "$STAGE/$NAME/README.md"
install -m 0644 "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
(
  cd "$STAGE/$NAME"
  shasum -a 256 xpad-install > SHA256SUMS
)
find "$STAGE/$NAME" -exec touch -t 200001010000 {} +

mkdir -p "$(dirname -- "$OUTPUT")"
rm -f "$OUTPUT"
(
  cd "$STAGE"
  COPYFILE_DISABLE=1 zip -q -X "$OUTPUT" \
    "$NAME/" \
    "$NAME/xpad-install" \
    "$NAME/README.md" \
    "$NAME/LICENSE" \
    "$NAME/SHA256SUMS"
)

echo "Built release archive: $OUTPUT"
