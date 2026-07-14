/*
 * Copyright (C) 2026 yoyicue
 * SPDX-License-Identifier: GPL-3.0-only
 */

.section .rodata.xpad_dex,"a",@progbits
.balign 16
.global xpad_dex_start
xpad_dex_start:
.incbin "exploit/xpad_installer.dex"
.global xpad_dex_end
xpad_dex_end:
.byte 0
