#!/usr/bin/env bash
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=2.5.0
SOURCE="$ROOT/windows-toolkit"
DIST="$ROOT/dist"
NAME="xpad-safe-install-toolkit-v$VERSION"
ZIP="$DIST/$NAME.zip"
EXPECTED_TOOL_SHA=014b8095f637e3a70b16ac6bca9a6f596bc239d62167a4508d50d136014410c5

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

[[ -f "$DIST/xpad-install" ]] || {
  printf 'xpad-install missing: %s\n' "$DIST/xpad-install" >&2
  exit 1
}
[[ $(sha256_file "$DIST/xpad-install") == "$EXPECTED_TOOL_SHA" ]] || {
  printf 'xpad-install SHA-256 mismatch\n' >&2
  exit 1
}

STAGE_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/xpad-safe-install-toolkit.XXXXXX")
trap 'rm -rf "$STAGE_ROOT"' EXIT HUP INT TERM
STAGE="$STAGE_ROOT/$NAME"

rm -f "$ZIP"
mkdir -p "$STAGE"
install -m 0755 "$DIST/xpad-install" "$STAGE/xpad-install"
install -m 0644 "$SOURCE/xpad-safe-install-gui.py" "$STAGE/xpad-safe-install-gui.py"
install -m 0644 "$SOURCE/README.md" "$STAGE/README.md"
install -m 0644 "$ROOT/LICENSE" "$STAGE/LICENSE"
# cmd.exe accepts LF, but CRLF avoids inconsistent behavior in older shells.
awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' \
  "$SOURCE/xpad-safe-install-gui.bat" > "$STAGE/xpad-safe-install-gui.bat"
(
  cd "$STAGE"
  for file in LICENSE README.md xpad-install xpad-safe-install-gui.bat \
    xpad-safe-install-gui.py; do
    printf '%s  %s\n' "$(sha256_file "$file")" "$file"
  done > SHA256SUMS
)
find "$STAGE" -exec touch -t 200001010000 {} +
(
  cd "$STAGE_ROOT"
  COPYFILE_DISABLE=1 zip -X -q "$ZIP" \
    "$NAME/" \
    "$NAME/LICENSE" \
    "$NAME/README.md" \
    "$NAME/xpad-install" \
    "$NAME/xpad-safe-install-gui.bat" \
    "$NAME/xpad-safe-install-gui.py" \
    "$NAME/SHA256SUMS"
)
unzip -tq "$ZIP" >/dev/null
printf 'XPAD_WINDOWS_TOOLKIT_OK version=%s sha256=%s output=%s\n' \
  "$VERSION" "$(sha256_file "$ZIP")" "$ZIP"
