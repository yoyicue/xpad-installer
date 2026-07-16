@echo off
rem Copyright (C) 2026 yoyicue
rem SPDX-License-Identifier: GPL-3.0-only
setlocal
chcp 65001 >nul
title xpad safe install tool v2.7.0
pushd "%~dp0" || (echo [ERROR] Cannot enter toolkit directory.& pause & exit /b 1)

where py >nul 2>nul && (py -3 "%~dp0xpad-safe-install-gui.py" & goto :done)
where python >nul 2>nul && (python "%~dp0xpad-safe-install-gui.py" & goto :done)
where python3 >nul 2>nul && (python3 "%~dp0xpad-safe-install-gui.py" & goto :done)

echo [ERROR] Python 3.10 or newer was not found.
set "RC=1"
goto :finish

:done
set "RC=%ERRORLEVEL%"
:finish
if not "%RC%"=="0" pause
popd
exit /b %RC%
