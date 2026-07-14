#!/bin/sh
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-$ROOT/dist/xpad-install}
NDK_ROOT=${NDK_ROOT:-/opt/homebrew/share/android-ndk}
HOST_TAG=${NDK_HOST_TAG:-darwin-x86_64}
CC=${CC:-$NDK_ROOT/toolchains/llvm/prebuilt/$HOST_TAG/bin/aarch64-linux-android33-clang}

"$ROOT/scripts/build_installer_dex.sh"
mkdir -p "$(dirname -- "$OUT")"

cd "$ROOT"
"$CC" -Oz -fPIE -pie -Wall -Wextra -Werror \
  -Wl,--build-id=sha1 -Wl,--gc-sections \
  native/xpad_install.c native/embed_dex.S -o "$OUT"

chmod 0755 "$OUT"
echo "Built single-file Android ELF: $OUT"
