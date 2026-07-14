# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

.PHONY: build test verify package clean

PYTHON ?= python3

build:
	./scripts/build_single_elf.sh

test:
	$(PYTHON) -m unittest discover -s tests -v

verify: test build

package: test
	./scripts/package_release.sh

clean:
	rm -rf dist .pytest_cache tests/__pycache__
