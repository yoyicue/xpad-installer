# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class StandaloneProjectTests(unittest.TestCase):
    def test_key_sources_declare_gpl3(self):
        sources = (
            "native/xpad_install.c",
            "native/embed_dex.S",
            "exploit/DirectInstaller.java",
            "exploit/ZnxxProviderInstaller.java",
            "exploit/Znxrun0044.java",
            "exploit/XpadInstaller.java",
            "scripts/build_installer_dex.sh",
            "scripts/build_single_elf.sh",
            "scripts/package_release.sh",
            "tests/test_project.py",
            "Makefile",
        )
        for relative in sources:
            header = (ROOT / relative).read_text()[:512]
            self.assertIn("Copyright (C) 2026 yoyicue", header, relative)
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", header, relative)

    def test_required_sources_are_present(self):
        required = (
            "native/xpad_install.c",
            "native/embed_dex.S",
            "exploit/DirectInstaller.java",
            "exploit/ZnxxProviderInstaller.java",
            "exploit/Znxrun0044.java",
            "exploit/XpadInstaller.java",
            "scripts/build_installer_dex.sh",
            "scripts/build_single_elf.sh",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)

    def test_generated_dex_files_have_dex_headers(self):
        for name in (
            "installer.dex",
            "znxx_provider_installer.dex",
            "xpad_installer.dex",
        ):
            path = ROOT / "exploit" / name
            self.assertEqual(path.read_bytes()[:4], b"dex\n", name)

    def test_hardened_dual_zygote_constants_are_preserved(self):
        source = (ROOT / "native/xpad_install.c").read_text()
        expected = (
            "#define ZYGOTE_WRITER_SIZE 8192",
            "#define PAYLOAD_ENTRY_COUNT 3000",
            'PRIMARY_TRIGGER_PACKAGE "com.android.settings"',
            'SECONDARY_TRIGGER_PACKAGE "com.tal.init.ota"',
            "cleanup_31317();",
            "cleanup_system_runner();",
        )
        for value in expected:
            self.assertIn(value, source)

    def test_cli_surface_is_preserved(self):
        source = (ROOT / "native/xpad_install.c").read_text()
        for command in (
            "doctor",
            "install",
            "upgrade",
            "verify",
            "activate",
            "autostart",
            "cleanup",
        ):
            self.assertIn(f'"  xpad-install {command}', source)

    def test_extraction_has_no_monorepo_absolute_path(self):
        suffixes = {".c", ".S", ".java", ".sh", ".md", ".py"}
        old_root = str(Path.home() / "xpad2")
        for path in ROOT.rglob("*"):
            if path.is_file() and path.suffix in suffixes:
                text = path.read_text(errors="replace")
                self.assertNotIn(old_root, text, str(path))


if __name__ == "__main__":
    unittest.main()
