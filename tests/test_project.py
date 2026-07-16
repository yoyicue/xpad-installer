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
            "scripts/package_windows_recovery.sh",
            "tools/package_windows_toolkit.sh",
            "tools/release_version_gate.py",
            "windows/xpad2-lockscreen-recovery.bat",
            "windows-toolkit/xpad-safe-install-gui.bat",
            "windows-toolkit/xpad-safe-install-gui.py",
            "tests/windows/FakeAdb.cs",
            "tests/windows/test_recovery.ps1",
            "tests/test_project.py",
            "tests/test_windows_toolkit.py",
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
            "scripts/package_windows_recovery.sh",
            "tools/package_windows_toolkit.sh",
            "tools/release_version_gate.py",
            "docs/USAGE.zh-CN.md",
            "windows/xpad2-lockscreen-recovery.bat",
            "windows/README-LOCKSCREEN-RECOVERY.zh-CN.txt",
            "windows-toolkit/xpad-safe-install-gui.bat",
            "windows-toolkit/xpad-safe-install-gui.py",
            "windows-toolkit/README.md",
            ".github/workflows/windows-recovery.yml",
            "tests/windows/FakeAdb.cs",
            "tests/windows/test_recovery.ps1",
            "VERSION",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)

    def test_every_release_packager_runs_the_version_gate(self):
        gate = "tools/release_version_gate.py"
        self.assertIn("version-gate", (ROOT / "Makefile").read_text())
        for relative in (
            "scripts/package_release.sh",
            "scripts/package_windows_recovery.sh",
            "tools/package_windows_toolkit.sh",
        ):
            self.assertIn(gate, (ROOT / relative).read_text(), relative)

    def test_recovery_release_version_is_explicitly_pinned(self):
        script = (ROOT / "scripts/package_windows_recovery.sh").read_text()
        self.assertIn("RECOVERY_VERSION=0.2.2", script)
        self.assertIn(
            'NAME="xpad-installer-v$RECOVERY_VERSION-windows-lockscreen-recovery"',
            script,
        )
        self.assertNotIn(
            'NAME="xpad-installer-v$VERSION-windows-lockscreen-recovery"',
            script,
        )

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
            "for (int attempt = 1; attempt <= 3; attempt++)",
            "cleanup_system_runner();",
            "incident_core_check",
            "trip_circuit_breaker",
            "finish_hidden_guard",
        )
        for value in expected:
            self.assertIn(value, source)

    def test_cli_surface_is_preserved(self):
        source = (ROOT / "native/xpad_install.c").read_text()
        for command in (
            "self-test",
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

    def test_read_only_commands_return_before_any_31317_fallback(self):
        source = (ROOT / "native/xpad_install.c").read_text()
        main = source[source.index("int main(int argc, char **argv)") :]
        fallback = main.index("system_transport(argc, argv)")
        for branch in (
            'return native_self_test();',
            'return native_doctor();',
            'return native_verify(argc, argv);',
            'return native_cleanup();',
            'return znxrun_status(1);',
        ):
            self.assertLess(main.index(branch), fallback, branch)
        self.assertNotIn("cleanup_31317", source)

    def test_31317_state_is_durable_and_exactly_restored(self):
        source = (ROOT / "native/xpad_install.c").read_text()
        for value in (
            'HIDDEN_SETTING_BACKUP',
            'CIRCUIT_BREAKER',
            'INCIDENT_LOG_DIR',
            'settings_get_global_exact',
            'settings_put_verified',
            'fsync(incident_fd)',
            '"core-pid-changed"',
            'return acquire_rc == 75 ? 75 : 77;',
        ):
            self.assertIn(value, source)
        self.assertNotIn(
            'settings delete global " HIDDEN_SETTING',
            source,
        )

    def test_managed_0044_anchor_is_bounded_and_embedded(self):
        manifest = (ROOT / "carrier/AndroidManifest.xml").read_text()
        self.assertIn('package="com.yoyicue.xpad2.installeranchor"', manifest)
        self.assertIn('android:hasCode="false"', manifest)
        self.assertIn('android:debuggable="false"', manifest)
        self.assertIn('android:versionCode="2"', manifest)
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
            "wait_znxrun_healthy(61, 5000000)",
            "ZNXRUN_SETTLE result=pending",
            "ZNXRUN_SETTLE",
        ):
            self.assertIn(value, native)
        self.assertIn("DirectInstaller.doInstall(apkPath, ZNXX + \"\\n\" + alias", java)
        self.assertIn('sameVersion ? "inherit-repair" : "full-upgrade"', java)
        self.assertIn("String inheritPackage = sameVersion ? archive.packageName : null", java)
        self.assertIn("PackageInstaller.SessionParams.MODE_INHERIT_EXISTING", direct)
        self.assertNotIn("Settings.Global.putString", java)

    def test_managed_0044_identity_tracks_the_device_oem_uid(self):
        native = (ROOT / "native/xpad_install.c").read_text()
        java = (ROOT / "exploit/Znxrun0044.java").read_text()
        status = native[native.index("static int znxrun_status") :]
        status = status[:status.index("static int rpc")]
        self.assertIn("lookup_oem_installer_uid", native)
        self.assertIn("parse_package_uid", native)
        self.assertIn("alias_uid == expected_uid", status)
        self.assertIn("expected_uid=%s", status)
        self.assertIn("app.uid", java)
        self.assertNotIn("ZNXRUN_ALIAS_LINE", native)
        self.assertNotIn('!strcmp(uid_output, "10072")', native)

    def test_every_apk_operation_uses_0044_and_31317_only_repairs_it(self):
        native = (ROOT / "native/xpad_install.c").read_text()
        java = (ROOT / "exploit/XpadInstaller.java").read_text()
        install_path = native[native.index("static int install_via_managed_0044") :]
        install_path = install_path[:install_path.index("int main(")]
        self.assertIn("ensure_znxrun(argv[0])", install_path)
        self.assertIn("run_java_as_znxrun", install_path)
        self.assertIn("target APK was not installed", install_path)
        self.assertIn("31317 target-APK fallback is disabled", install_path)
        self.assertNotIn("system_transport", install_path)
        self.assertNotIn("ionstack_delegate", install_path)
        self.assertIn("if (znxrun_status(0) == 0) return 0;", native)
        self.assertIn("int repair = ensure_znxrun(executable)", native)
        self.assertIn("repair == ZNXRUN_ENSURE_PENDING", native)
        self.assertIn("if (!ok && Process.myUid() != 0)", java)
        self.assertIn("provider did not commit; trying direct backend in current identity", java)

    def test_unknown_commands_cannot_reach_a_privileged_transport(self):
        native = (ROOT / "native/xpad_install.c").read_text()
        main = native[native.index("int main(int argc, char **argv)") :]
        rejection = main.index("xpad-install: unknown command")
        self.assertLess(rejection, main.index("ionstack_delegate(argc, argv)"))
        self.assertLess(rejection, main.index("system_transport(argc, argv)"))
        self.assertIn('!strcmp(argv[1], "--version")', main)

    def test_boominstaller_activation_never_uses_installer_identities(self):
        source = (ROOT / "native/xpad_install.c").read_text()
        main = source[source.index("int main(int argc, char **argv)") :]
        activate = main[main.index('if (!strcmp(argv[1], "activate"))') :]
        activate = activate[:activate.index(
            'if (!strcmp(argv[1], "install")')]
        self.assertIn("return serve(argc - 1, argv + 1);", activate)
        self.assertNotIn("activate_as_znxrun", source)
        self.assertNotIn("activate_as_system", source)
        self.assertIn("uid != 0 && uid != 2000", source)
        self.assertNotIn("acquire_31317", activate)

    def test_extraction_has_no_monorepo_absolute_path(self):
        suffixes = {".c", ".S", ".java", ".sh", ".md", ".py"}
        old_root = str(Path.home() / "xpad2")
        for path in ROOT.rglob("*"):
            if path.is_file() and path.suffix in suffixes:
                text = path.read_text(errors="replace")
                self.assertNotIn(old_root, text, str(path))

    def test_windows_recovery_is_bounded_and_credential_safe(self):
        batch = (ROOT / "windows/xpad2-lockscreen-recovery.bat").read_text()
        expected = (
            "EXPECTED_FINGERPRINT=alps/vnd_ls12_mt8797_wifi_64/",
            "EXPECTED_TOOL_HASH=9f1ff6b7635548a11c57b2b8a31b0b98b941773bc6e0f2f00a5c3dc98e3a5fc0",
            "self-test",
            "cleanup",
            "locksettings get-disabled --user 0",
            "locksettings set-disabled --user 0 false",
            "dumpsys lock_settings",
            "dumpsys window policy",
            "hidden_setting_sha256_without_disclosure",
            "Compress-Archive",
            "<redacted-serial>",
            "reboot",
        )
        for value in expected:
            self.assertIn(value, batch, value)
        forbidden = (
            " xpad-install doctor",
            "locksettings clear",
            "locksettings set-pin",
            "locksettings set-pattern",
            "locksettings set-password",
            "settings put secure",
            "curl ",
            "Invoke-WebRequest",
        )
        for value in forbidden:
            self.assertNotIn(value, batch, value)

    def test_windows_recovery_package_excludes_adb(self):
        package = (ROOT / "scripts/package_windows_recovery.sh").read_text()
        self.assertIn("START-LOCKSCREEN-RECOVERY.bat", package)
        self.assertIn("README-FIRST.zh-CN.txt", package)
        self.assertNotIn("adb.exe", package)
        self.assertNotIn("platform-tools", package)


if __name__ == "__main__":
    unittest.main()
