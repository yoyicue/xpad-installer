# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

.PHONY: build carrier verify-carrier test verify package windows-recovery-package windows-toolkit-package clean

PYTHON ?= python3

build: verify-carrier
	./scripts/build_single_elf.sh

carrier:
	./scripts/sign_carrier_apk.sh

verify-carrier:
	./scripts/verify_carrier_apk.sh

test:
	$(PYTHON) -m unittest discover -s tests -v

verify: test build

package: test
	./scripts/package_release.sh

windows-recovery-package: test
	./scripts/package_windows_recovery.sh

windows-toolkit-package: test build
	./tools/package_windows_toolkit.sh

clean:
	rm -rf dist .pytest_cache tests/__pycache__
