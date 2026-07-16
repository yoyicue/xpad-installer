@echo off
rem Copyright (C) 2026 yoyicue
rem SPDX-License-Identifier: GPL-3.0-only
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul
title XPad2 锁屏安全恢复工具

pushd "%~dp0" || (
  echo [错误] 无法进入恢复包目录。
  pause
  exit /b 1
)

set "TOOL_VERSION=0.2.2"
set "EXPECTED_TOOL_HASH=9f1ff6b7635548a11c57b2b8a31b0b98b941773bc6e0f2f00a5c3dc98e3a5fc0"
set "EXPECTED_FINGERPRINT=alps/vnd_ls12_mt8797_wifi_64/ls12_mt8797_wifi_64:13/TP1A.220624.014/260:user/release-keys"
set "REMOTE_TOOL=/data/local/tmp/xpad-install-recovery-v0.2.2"
set "RUN_ID=%RANDOM%-%RANDOM%"
set "LOGDIR=%~dp0xpad2-lockscreen-recovery-%RUN_ID%"
set "LOGZIP=%~dp0xpad2-lockscreen-recovery-log-%RUN_ID%.zip"
set "SUMMARY=!LOGDIR!\summary.txt"
set "FAIL_REASON=未说明的错误"
set "SERIAL="

mkdir "!LOGDIR!" >nul 2>&1
if errorlevel 1 (
  echo [错误] 无法创建诊断目录：!LOGDIR!
  pause
  popd
  exit /b 1
)

>"!SUMMARY!" echo XPad2 lockscreen recovery
>>"!SUMMARY!" echo tool_version=!TOOL_VERSION!
>>"!SUMMARY!" echo started=%date% %time%

echo.
echo ============================================================
echo        XPad2 锁屏安全恢复工具（仅支持 /260 固件）
echo ============================================================
echo 本工具会先保存诊断，再清理旧版 doctor 遗留的 31317 状态，
echo 然后普通重启并打开系统安全设置。
echo.
echo 它不会清除、设置或猜测 PIN、图案、密码。
echo.

set "ADB="
if exist "%~dp0adb.exe" set "ADB=%~dp0adb.exe"
if not defined ADB if exist "%~dp0platform-tools\adb.exe" set "ADB=%~dp0platform-tools\adb.exe"
if not defined ADB for %%A in (adb.exe) do if not "%%~$PATH:A"=="" set "ADB=%%~$PATH:A"
if not defined ADB (
  set "FAIL_REASON=找不到 adb.exe。请把恢复包解压到 Android platform-tools 目录后再双击。"
  goto :failure
)

"!ADB!" version >>"!SUMMARY!" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=adb.exe 无法运行。"
  goto :failure
)

if not exist "%~dp0xpad-install" (
  set "FAIL_REASON=恢复包缺少 xpad-install。请重新下载并完整解压。"
  goto :failure
)

certutil -hashfile "%~dp0xpad-install" SHA256 >"!LOGDIR!\host-binary-sha256.txt" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=无法校验 xpad-install。"
  goto :failure
)
findstr /I /C:"!EXPECTED_TOOL_HASH!" "!LOGDIR!\host-binary-sha256.txt" >nul
if errorlevel 1 (
  set "FAIL_REASON=xpad-install 校验失败，文件可能损坏或被替换。"
  goto :failure
)

"!ADB!" start-server >>"!SUMMARY!" 2>&1
set "DEVICE_FILE=%TEMP%\xpad2-recovery-devices-!RUN_ID!.txt"
"!ADB!" devices -l >"!DEVICE_FILE!" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=无法读取设备列表。"
  goto :failure
)

set /a DEVICE_COUNT=0
set /a UNAUTHORIZED_COUNT=0
for /f "usebackq skip=1 tokens=1,2" %%A in ("!DEVICE_FILE!") do (
  if /I "%%B"=="device" (
    set /a DEVICE_COUNT+=1
    set "SERIAL=%%A"
  )
  if /I "%%B"=="unauthorized" set /a UNAUTHORIZED_COUNT+=1
)
del "!DEVICE_FILE!" >nul 2>&1

if !DEVICE_COUNT! EQU 0 (
  if !UNAUTHORIZED_COUNT! GTR 0 (
    set "FAIL_REASON=平板尚未授权。请解锁平板，在 USB 调试弹窗中点“允许”，再运行本工具。"
  ) else (
    set "FAIL_REASON=没有找到已连接的平板。请连接 USB 并打开 USB 调试。"
  )
  goto :failure
)
if !DEVICE_COUNT! GTR 1 (
  set "FAIL_REASON=检测到多台设备。请只保留需要恢复的这一台平板。"
  goto :failure
)

"!ADB!" -s "!SERIAL!" shell getprop ro.build.fingerprint >"!LOGDIR!\build-fingerprint.txt" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=无法读取平板固件信息。"
  goto :failure
)
set "FINGERPRINT="
set /p FINGERPRINT=<"!LOGDIR!\build-fingerprint.txt"
if /I not "!FINGERPRINT!"=="!EXPECTED_FINGERPRINT!" (
  set "FAIL_REASON=固件不是受支持的 XPad2 /260，已停止且未修改设备。"
  goto :failure
)
>>"!SUMMARY!" echo firmware=260-supported
>>"!SUMMARY!" echo device=authorized-single-device

echo [1/6] 保存恢复前诊断……
call :capture_state before
call :capture_incident_logs before
call :read_lock_state PRE

echo.
echo 即将执行：
echo   1. 推送并校验 xpad-install v!TOOL_VERSION!
echo   2. 只运行新版 self-test 和 cleanup
echo   3. 必要时恢复“非安全滑动锁屏”开关
echo   4. 普通重启一次
echo.
choice /C YN /N /M "开始恢复并重启？[Y/N] "
if errorlevel 2 goto :cancelled

echo [2/6] 部署并校验新版安全清理器……
"!ADB!" -s "!SERIAL!" push "%~dp0xpad-install" "!REMOTE_TOOL!" >"!LOGDIR!\push.txt" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=无法把安全清理器传到平板。"
  goto :failure
)
"!ADB!" -s "!SERIAL!" shell chmod 700 "!REMOTE_TOOL!" >>"!LOGDIR!\push.txt" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=无法设置安全清理器权限。"
  goto :failure
)
"!ADB!" -s "!SERIAL!" shell "toybox sha256sum '!REMOTE_TOOL!'" >"!LOGDIR!\device-binary-sha256.txt" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=无法在平板上校验安全清理器。"
  goto :failure
)
findstr /I /B /C:"!EXPECTED_TOOL_HASH!" "!LOGDIR!\device-binary-sha256.txt" >nul
if errorlevel 1 (
  set "FAIL_REASON=平板上的安全清理器校验失败。"
  goto :failure
)

"!ADB!" -s "!SERIAL!" shell "'!REMOTE_TOOL!' self-test" >"!LOGDIR!\self-test.txt" 2>&1
set "SELF_TEST_RC=!errorlevel!"
type "!LOGDIR!\self-test.txt"
if not "!SELF_TEST_RC!"=="0" (
  set "FAIL_REASON=xpad-install 自检失败，未执行清理。"
  goto :failure
)
findstr /C:"status=ok" "!LOGDIR!\self-test.txt" >nul
if errorlevel 1 (
  set "FAIL_REASON=xpad-install 自检结果不完整，未执行清理。"
  goto :failure
)
findstr /C:"version=!TOOL_VERSION!" "!LOGDIR!\self-test.txt" >nul
if errorlevel 1 (
  set "FAIL_REASON=xpad-install 版本不正确，未执行清理。"
  goto :failure
)

echo [3/6] 清理旧版 31317 临时状态……
"!ADB!" -s "!SERIAL!" shell "'!REMOTE_TOOL!' cleanup" >"!LOGDIR!\cleanup.txt" 2>&1
set "CLEANUP_RC=!errorlevel!"
type "!LOGDIR!\cleanup.txt"
>>"!SUMMARY!" echo cleanup_exit=!CLEANUP_RC!
if not "!CLEANUP_RC!"=="0" if not "!CLEANUP_RC!"=="75" (
  set "FAIL_REASON=安全清理返回异常代码 !CLEANUP_RC!，为避免扩大问题，本工具没有自动重启。"
  goto :failure
)

if "!PRE_DISABLED!"=="1" if "!PRE_NO_CREDENTIAL!"=="1" (
  echo 检测到无凭据且锁屏服务被禁用，恢复非安全滑动锁屏……
  "!ADB!" -s "!SERIAL!" shell locksettings set-disabled --user 0 false >"!LOGDIR!\enable-swipe-lockscreen-before-reboot.txt" 2>&1
  set "ENABLE_LOCK_RC=!errorlevel!"
  >>"!SUMMARY!" echo enable_swipe_before_reboot_exit=!ENABLE_LOCK_RC!
)

"!ADB!" -s "!SERIAL!" shell rm -f "!REMOTE_TOOL!" >>"!LOGDIR!\cleanup.txt" 2>&1

echo [4/6] 普通重启平板……
"!ADB!" -s "!SERIAL!" reboot >>"!SUMMARY!" 2>&1
if errorlevel 1 (
  set "FAIL_REASON=清理已经执行，但发送普通重启命令失败。请手动重启平板。"
  goto :failure
)

set /a WAIT_COUNT=0
:wait_for_disconnect
set /a WAIT_COUNT+=1
"!ADB!" -s "!SERIAL!" get-state >nul 2>&1
if errorlevel 1 goto :wait_for_boot
if !WAIT_COUNT! GEQ 15 goto :wait_for_boot
timeout /t 1 /nobreak >nul
goto :wait_for_disconnect

:wait_for_boot
echo [5/6] 等待平板启动完成（最长约 3 分钟）……
set /a WAIT_COUNT=0
:wait_for_boot_loop
set /a WAIT_COUNT+=1
"!ADB!" -s "!SERIAL!" shell getprop sys.boot_completed >"!LOGDIR!\boot-completed.txt" 2>nul
findstr /X /C:"1" "!LOGDIR!\boot-completed.txt" >nul 2>&1
if not errorlevel 1 goto :boot_ready
if !WAIT_COUNT! GEQ 90 goto :boot_timeout
timeout /t 2 /nobreak >nul
goto :wait_for_boot_loop

:boot_ready
echo [6/6] 保存重启后状态并打开安全设置……
call :capture_state after
call :capture_incident_logs after
call :read_lock_state POST

if "!POST_DISABLED!"=="1" if "!POST_NO_CREDENTIAL!"=="1" (
  "!ADB!" -s "!SERIAL!" shell locksettings set-disabled --user 0 false >"!LOGDIR!\enable-swipe-lockscreen-after-reboot.txt" 2>&1
  set "ENABLE_LOCK_POST_RC=!errorlevel!"
  >>"!SUMMARY!" echo enable_swipe_after_reboot_exit=!ENABLE_LOCK_POST_RC!
)

"!ADB!" -s "!SERIAL!" shell "am start -n 'com.android.settings/.Settings$SecurityDashboardActivity'" >"!LOGDIR!\open-security-settings.txt" 2>&1
if errorlevel 1 "!ADB!" -s "!SERIAL!" shell am start -a android.settings.SETTINGS >>"!LOGDIR!\open-security-settings.txt" 2>&1

>>"!SUMMARY!" echo completed=%date% %time%
>>"!SUMMARY!" echo result=completed
call :make_log_zip

echo.
echo ============================================================
echo [完成] 31317 安全清理和普通重启已经完成。
echo ============================================================
echo 平板已打开“安全”设置：
echo   - 只需要普通锁屏：选择“滑动”。
echo   - 需要 PIN/图案：请由用户本人在设置中重新设置。
echo.
echo 请按一次电源键熄屏，再唤醒确认锁屏是否恢复。
if exist "!LOGZIP!" (
  echo 如仍异常，把这个诊断包发回：
  echo !LOGZIP!
) else (
  echo 自动压缩失败；诊断文件仍保存在：
  echo !LOGDIR!
)
echo.
pause
popd
exit /b 0

:boot_timeout
>>"!SUMMARY!" echo result=boot-timeout
set "FAIL_REASON=清理和重启命令已经执行，但 3 分钟内没有重新连上平板。请等待启动完成后重新连接 USB。"
goto :failure

:cancelled
>>"!SUMMARY!" echo result=cancelled
call :make_log_zip
echo 已取消，设备没有被修改或重启。
echo 恢复前诊断保存在：!LOGDIR!
pause
popd
exit /b 0

:failure
>>"!SUMMARY!" echo result=failed
>>"!SUMMARY!" echo failure=!FAIL_REASON!
call :make_log_zip
echo.
echo [错误] !FAIL_REASON!
if exist "!LOGZIP!" echo 诊断包：!LOGZIP!
echo.
pause
popd
exit /b 1

:capture_state
set "PHASE=%~1"
set "STATE_FILE=!LOGDIR!\!PHASE!-state.txt"
>"!STATE_FILE!" echo phase=!PHASE!
>>"!STATE_FILE!" echo captured=%date% %time%
>>"!STATE_FILE!" echo.
>>"!STATE_FILE!" echo [boot_id]
"!ADB!" -s "!SERIAL!" shell cat /proc/sys/kernel/random/boot_id >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [selinux]
"!ADB!" -s "!SERIAL!" shell getenforce >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [zygote64_pid]
"!ADB!" -s "!SERIAL!" shell pidof zygote64 >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [zygote32_pid]
"!ADB!" -s "!SERIAL!" shell pidof zygote >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [system_server_pid]
"!ADB!" -s "!SERIAL!" shell pidof system_server >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [systemui_pid]
"!ADB!" -s "!SERIAL!" shell pidof com.android.systemui >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [lockscreen_disabled_service]
"!ADB!" -s "!SERIAL!" shell locksettings get-disabled --user 0 >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [hidden_setting_byte_count_including_cli_newline]
"!ADB!" -s "!SERIAL!" shell "settings get global hidden_api_blacklist_exemptions | wc -c" >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [hidden_setting_sha256_without_disclosure]
"!ADB!" -s "!SERIAL!" shell "settings get global hidden_api_blacklist_exemptions | toybox sha256sum" >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [hidden_setting_setuid_marker_exit]
"!ADB!" -s "!SERIAL!" shell "settings get global hidden_api_blacklist_exemptions | grep -q -- '--setuid=1000'; echo $?" >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [hidden_setting_listener_marker_exit]
"!ADB!" -s "!SERIAL!" shell "settings get global hidden_api_blacklist_exemptions | grep -q -- '127.0.0.1 -p 8888'; echo $?" >>"!STATE_FILE!" 2>&1
>>"!STATE_FILE!" echo [port_8888]
"!ADB!" -s "!SERIAL!" shell "toybox netstat -lnt 2>/dev/null | grep ':8888 '" >>"!STATE_FILE!" 2>&1

"!ADB!" -s "!SERIAL!" shell dumpsys lock_settings >"!LOGDIR!\!PHASE!-lock-settings.txt" 2>&1
"!ADB!" -s "!SERIAL!" shell dumpsys window policy >"!LOGDIR!\!PHASE!-window-policy.txt" 2>&1
"!ADB!" -s "!SERIAL!" shell "logcat -b all -d -v threadtime | grep -iE 'zygote|system_server|systemui|keyguard|locksettings|xpad-install|31317|hidden_api'" >"!LOGDIR!\!PHASE!-relevant-logcat.txt" 2>&1
"!ADB!" -s "!SERIAL!" shell dumpsys dropbox --print system_server_crash >"!LOGDIR!\!PHASE!-dropbox-system-server-crash.txt" 2>&1
"!ADB!" -s "!SERIAL!" shell dumpsys dropbox --print system_app_crash >"!LOGDIR!\!PHASE!-dropbox-system-app-crash.txt" 2>&1
exit /b 0

:capture_incident_logs
set "PHASE=%~1"
mkdir "!LOGDIR!\!PHASE!-31317-incident-logs" >nul 2>&1
"!ADB!" -s "!SERIAL!" pull /data/local/tmp/.xpad-installer/logs "!LOGDIR!\!PHASE!-31317-incident-logs" >"!LOGDIR!\!PHASE!-incident-pull.txt" 2>&1
exit /b 0

:read_lock_state
set "%~1_DISABLED=0"
set "%~1_NO_CREDENTIAL=0"
set "LOCK_DISABLED_FILE=!LOGDIR!\%~1-lockscreen-disabled.txt"
set "LOCK_DUMP_FILE=!LOGDIR!\%~1-lock-settings-check.txt"
"!ADB!" -s "!SERIAL!" shell locksettings get-disabled --user 0 >"!LOCK_DISABLED_FILE!" 2>&1
findstr /I /X /C:"true" "!LOCK_DISABLED_FILE!" >nul 2>&1
if not errorlevel 1 set "%~1_DISABLED=1"
"!ADB!" -s "!SERIAL!" shell dumpsys lock_settings >"!LOCK_DUMP_FILE!" 2>&1
findstr /C:"CredentialType: None" "!LOCK_DUMP_FILE!" >nul 2>&1
if not errorlevel 1 set "%~1_NO_CREDENTIAL=1"
exit /b 0

:make_log_zip
set "XPAD_RECOVERY_LOGDIR=!LOGDIR!"
set "XPAD_RECOVERY_LOGZIP=!LOGZIP!"
set "XPAD_RECOVERY_SERIAL=!SERIAL!"
where powershell.exe >nul 2>&1
if errorlevel 1 exit /b 1
powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $utf8=New-Object System.Text.UTF8Encoding($false); Get-ChildItem -LiteralPath $env:XPAD_RECOVERY_LOGDIR -File -Recurse | ForEach-Object { $text=[IO.File]::ReadAllText($_.FullName); if ($env:XPAD_RECOVERY_SERIAL) { $text=$text.Replace($env:XPAD_RECOVERY_SERIAL,'<redacted-serial>') }; [IO.File]::WriteAllText($_.FullName,$text,$utf8) }; Compress-Archive -Path (Join-Path $env:XPAD_RECOVERY_LOGDIR '*') -DestinationPath $env:XPAD_RECOVERY_LOGZIP -Force" >nul 2>&1
exit /b !errorlevel!
