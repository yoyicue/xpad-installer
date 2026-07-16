#!/usr/bin/env python3
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only
"""Release gate for version and checksum drift in sources and ZIP artifacts."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEMVER = r"[0-9]+\.[0-9]+\.[0-9]+"


class Gate:
    def __init__(self):
        self.errors: list[str] = []

    def expect(self, condition: bool, message: str):
        if not condition:
            self.errors.append(message)

    def text(self, path: Path) -> str:
        try:
            return path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            self.errors.append(f"cannot read {path}: {exc}")
            return ""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def binary_version(data: bytes) -> str | None:
    matches = {m.decode() for m in re.findall(rb"(?<![0-9.])([0-9]+\.[0-9]+\.[0-9]+)(?![0-9.])", data)}
    plausible = sorted(v for v in matches if v.startswith("0."))
    return plausible[0] if len(plausible) == 1 else None


def assignment(text: str, name: str) -> str | None:
    match = re.search(rf'^\s*{re.escape(name)}\s*=\s*["\']?([^"\'\s]+)', text, re.MULTILINE)
    return match.group(1) if match else None


def check_sources(gate: Gate):
    engine = gate.text(ROOT / "VERSION").strip()
    gate.expect(bool(re.fullmatch(SEMVER, engine)), f"VERSION is not semver: {engine!r}")

    readme = gate.text(ROOT / "README.md")
    guide = gate.text(ROOT / "docs/USAGE.zh-CN.md")
    toolkit_readme = gate.text(ROOT / "windows-toolkit/README.md")
    toolkit_py = gate.text(ROOT / "windows-toolkit/xpad-safe-install-gui.py")
    toolkit_pack = gate.text(ROOT / "tools/package_windows_toolkit.sh")
    recovery_pack = gate.text(ROOT / "scripts/package_windows_recovery.sh")

    toolkit_version = assignment(toolkit_py, "VERSION")
    gate.expect(toolkit_version is not None, "GUI VERSION assignment missing")
    gate.expect(assignment(toolkit_pack, "VERSION") == toolkit_version,
                "Windows package VERSION differs from GUI VERSION")
    gate.expect(f"# xpad 安全安装工具 v{toolkit_version}" in toolkit_readme,
                "Windows README title differs from GUI VERSION")
    gate.expect(f"xpad-install` v{engine}" in toolkit_readme,
                "Windows README engine version differs from VERSION")
    gate.expect(f"xpad-installer-v{engine}-android-arm64.zip" in readme,
                "root README current release filename differs from VERSION")
    gate.expect(f"xpad-installer-v{engine}-android-arm64" in guide,
                "Chinese guide current release directory differs from VERSION")
    gate.expect(f"xpad-install` v{engine}" in readme,
                "root README Windows engine version differs from VERSION")

    recovery = assignment(recovery_pack, "RECOVERY_VERSION")
    gate.expect(recovery == "0.2.2", "recovery package must explicitly pin RECOVERY_VERSION=0.2.2")
    gate.expect('NAME="xpad-installer-v$RECOVERY_VERSION-windows-lockscreen-recovery"' in recovery_pack,
                "recovery archive name must use RECOVERY_VERSION, not root VERSION")

    dist_tool = ROOT / "dist/xpad-install"
    if dist_tool.is_file():
        embedded = binary_version(dist_tool.read_bytes())
        gate.expect(embedded == engine,
                    f"dist/xpad-install embeds {embedded!r}, expected VERSION {engine}")
        expected_hash = assignment(toolkit_py, "TOOL_SHA256")
        gate.expect(expected_hash == sha256(dist_tool.read_bytes()),
                    "GUI TOOL_SHA256 differs from dist/xpad-install")
        gate.expect(assignment(toolkit_pack, "EXPECTED_TOOL_SHA") == expected_hash,
                    "Windows packager hash differs from GUI TOOL_SHA256")


def manifest_entries(text: str) -> dict[str, str]:
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]{64})\s+\*?(.+)", line.strip())
        if match:
            result[match.group(2)] = match.group(1).lower()
    return result


def check_zip(gate: Gate, path: Path):
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        gate.errors.append(f"{path}: invalid ZIP: {exc}")
        return
    with archive:
        files = [name for name in archive.namelist() if not name.endswith("/")]
        roots = {name.split("/", 1)[0] for name in files if "/" in name}
        gate.expect(len(roots) == 1, f"{path.name}: expected exactly one top-level directory")
        root = next(iter(roots), "")
        gate.expect(root == path.stem, f"{path.name}: top directory {root!r} differs from ZIP stem")

        android = re.fullmatch(rf"xpad-installer-v({SEMVER})-android-arm64", path.stem)
        toolkit = re.fullmatch(rf"xpad-safe-install-toolkit-v({SEMVER})", path.stem)
        recovery = re.fullmatch(rf"xpad-installer-v({SEMVER})-windows-lockscreen-recovery", path.stem)
        if not (android or toolkit or recovery):
            return
        advertised = (android or toolkit or recovery).group(1)
        prefix = root + "/"
        tool_name = prefix + "xpad-install"
        if tool_name not in files:
            gate.errors.append(f"{path.name}: xpad-install missing")
            return
        tool = archive.read(tool_name)
        embedded = binary_version(tool)
        if android:
            gate.expect(embedded == advertised,
                        f"{path.name}: binary embeds {embedded!r}, archive advertises {advertised}")
        if recovery:
            gate.expect(advertised == "0.2.2",
                        f"{path.name}: recovery artifact must advertise pinned 0.2.2")
            gate.expect(embedded == advertised,
                        f"{path.name}: recovery binary embeds {embedded!r}, archive advertises {advertised}")

        readme_name = prefix + "README.md"
        if toolkit and readme_name in files:
            readme = archive.read(readme_name).decode("utf-8-sig", "replace")
            gate.expect(re.search(rf"^# .*v{re.escape(advertised)}\s*$", readme, re.MULTILINE) is not None,
                        f"{path.name}: README title differs from archive version")
            py_names = [n for n in files if n.endswith(".py")]
            if py_names:
                py = archive.read(py_names[0]).decode("utf-8-sig", "replace")
                gate.expect(assignment(py, "VERSION") == advertised,
                            f"{path.name}: Python VERSION differs from archive version")
                gate.expect(assignment(py, "TOOL_SHA256") == sha256(tool),
                            f"{path.name}: Python tool hash differs from bundled binary")
            if embedded:
                gate.expect(f"v{embedded}" in readme,
                            f"{path.name}: README does not identify bundled engine v{embedded}")

        manifests = [n for n in files if n.endswith("/SHA256SUMS")]
        gate.expect(len(manifests) == 1, f"{path.name}: expected one SHA256SUMS")
        if manifests:
            entries = manifest_entries(archive.read(manifests[0]).decode("ascii", "replace"))
            for name, expected in entries.items():
                member = prefix + name
                gate.expect(member in files, f"{path.name}: manifest member missing: {name}")
                if member in files:
                    gate.expect(sha256(archive.read(member)) == expected,
                                f"{path.name}: checksum mismatch: {name}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifacts", nargs="*", type=Path,
                        help="ZIPs to validate; omit to validate current source state")
    args = parser.parse_args(argv)
    gate = Gate()
    check_sources(gate)
    for artifact in args.artifacts:
        check_zip(gate, artifact.resolve())
    if gate.errors:
        for error in gate.errors:
            print(f"VERSION_GATE_ERROR: {error}", file=sys.stderr)
        return 1
    print(f"VERSION_GATE_OK artifacts={len(args.artifacts)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
