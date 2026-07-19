# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

import importlib.util
from pathlib import Path
import sys
import unittest


MODULE_PATH = Path(__file__).parents[1] / "windows-toolkit" / "xpad-safe-install-gui.py"
SPEC = importlib.util.spec_from_file_location("xpad_safe_install_gui", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class WindowsToolkitTests(unittest.TestCase):
    def test_toolkit_locks_current_engine(self):
        self.assertEqual(MODULE.VERSION, "2.11.0")
        self.assertEqual(
            MODULE.TOOL_SHA256,
            "641eb9d1d790397e3087a41b922cd8f807188ff382d42938f1beb55a10b2d743",
        )

    def test_permission_conflict_is_not_misclassified_as_reboot(self):
        advice = MODULE.classify_install_failure(
            "Status=1: -126: attempting to redeclare permission group already owned by other", 1
        )
        self.assertEqual(advice.kind, "permission-owner-conflict")
        self.assertFalse(advice.reboot_required)
        self.assertIn("r14", advice.message)

    def test_only_safety_circuit_breaker_requires_reboot(self):
        advice = MODULE.classify_install_failure("process is bad", 75)
        self.assertEqual(advice.kind, "reboot-required")
        self.assertTrue(advice.reboot_required)

    def test_generic_failure_preserves_no_reboot_default(self):
        advice = MODULE.classify_install_failure("INSTALL_FAILED_INVALID_APK", 1)
        self.assertEqual(advice.kind, "install-failed")
        self.assertFalse(advice.reboot_required)

    def test_staging_io_failure_preserves_errno_and_does_not_request_reboot(self):
        advice = MODULE.classify_install_failure(
            "xpad-install: staging failed artifact=managed-0044-apk "
            "path=/data/local/tmp/.xpad-znxrun.A1b2C3.apk errno=13 "
            "error=Permission denied",
            74,
        )
        self.assertEqual(advice.kind, "staging-io")
        self.assertFalse(advice.reboot_required)
        self.assertIn("artifact", advice.message)
        self.assertIn("errno", advice.message)

    def test_structured_error_does_not_infer_reboot_from_message_text(self):
        error = MODULE.InstallError("普通重启不能修复这个 APK", False)
        self.assertFalse(error.reboot_required)

    def test_package_parser_accepts_boominstaller(self):
        result = type(
            "Result",
            (),
            {
                "stdout": "package=com.yoyicue.boominstaller versionCode=14",
                "stderr": "",
            },
        )()
        self.assertEqual(MODULE.package_name(result), "com.yoyicue.boominstaller")


if __name__ == "__main__":
    unittest.main()
