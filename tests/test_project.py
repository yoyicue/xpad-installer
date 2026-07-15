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
            "carrier/AndroidManifest.xml",
            "scripts/build_installer_dex.sh",
            "scripts/build_carrier_apk.sh",
            "scripts/sign_carrier_apk.sh",
            "scripts/verify_carrier_apk.sh",
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
            "carrier/AndroidManifest.xml",
            "carrier/xpad2-installer-anchor.apk",
            "scripts/build_carrier_apk.sh",
            "scripts/build_installer_dex.sh",
            "scripts/sign_carrier_apk.sh",
            "scripts/verify_carrier_apk.sh",
            "scripts/build_single_elf.sh",
            "scripts/package_release.sh",
            "docs/USAGE.zh-CN.md",
            "VERSION",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)

    def test_beginner_guide_covers_public_cli_and_official_adb_docs(self):
        guide = (ROOT / "docs/USAGE.zh-CN.md").read_text()
        expected = (
            "adb devices -l",
            "adb -s SERIAL push",
            "xpad-install doctor",
            "xpad-install install",
            "xpad-install upgrade",
            "xpad-install verify",
            "xpad-install activate",
            "xpad-install autostart enable",
            "xpad-install znxrun status",
            "xpad-install znxrun ensure",
            "xpad-install znxrun preflight",
            "xpad-install znxrun create",
            "xpad-install cleanup",
            "https://developer.android.com/tools/adb",
            "https://developer.android.com/tools/releases/platform-tools",
        )
        for value in expected:
            self.assertIn(value, guide, value)

        package_script = (ROOT / "scripts/package_release.sh").read_text()
        self.assertIn("docs/USAGE.zh-CN.md", package_script)

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
            "znxrun status",
            "znxrun ensure",
        ):
            self.assertIn(f'"  xpad-install {command}', source)

    def test_managed_0044_anchor_is_bounded_and_embedded(self):
        manifest = (ROOT / "carrier/AndroidManifest.xml").read_text()
        self.assertIn('package="com.yoyicue.xpad2.installeranchor"', manifest)
        self.assertIn('android:hasCode="false"', manifest)
        self.assertIn('android:debuggable="false"', manifest)
        self.assertNotIn("uses-permission", manifest)
        self.assertNotIn("<activity", manifest)
        self.assertNotIn("<service", manifest)
        self.assertNotIn("<receiver", manifest)
        self.assertNotIn("<provider", manifest)

        embedded = (ROOT / "native/embed_dex.S").read_text()
        self.assertIn('.incbin "carrier/xpad2-installer-anchor.apk"', embedded)
        self.assertTrue((ROOT / "carrier/xpad2-installer-anchor.apk").read_bytes().startswith(b"PK"))

    def test_0044_persistence_has_outer_verification_and_whitelist_guard(self):
        native = (ROOT / "native/xpad_install.c").read_text()
        java = (ROOT / "exploit/Znxrun0044.java").read_text()
        direct = (ROOT / "exploit/DirectInstaller.java").read_text()
        for value in (
            "prepare_whitelist",
            "restore_whitelist",
            "ZNXRUN_STATUS",
            "installerPackageName=",
            "run_znxrun_mutation",
            "forward_guarded_signal",
        ):
            self.assertIn(value, native)
        self.assertIn("DirectInstaller.doInstall(apkPath, ZNXX + \"\\n\" + alias", java)
        self.assertIn("PackageInstaller.SessionParams.MODE_INHERIT_EXISTING", direct)
        self.assertNotIn("Settings.Global.putString", java)

    def test_extraction_has_no_monorepo_absolute_path(self):
        suffixes = {".c", ".S", ".java", ".sh", ".md", ".py"}
        old_root = str(Path.home() / "xpad2")
        for path in ROOT.rglob("*"):
            if path.is_file() and path.suffix in suffixes:
                text = path.read_text(errors="replace")
                self.assertNotIn(old_root, text, str(path))


if __name__ == "__main__":
    unittest.main()
