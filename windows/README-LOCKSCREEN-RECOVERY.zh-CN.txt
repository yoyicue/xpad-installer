XPad2 /260 锁屏安全恢复包
============================

适用情况
--------

用户曾运行 xpad-installer v0.2.1 或更早版本的 doctor，之后出现：

- 按电源键熄屏后，再唤醒直接回到桌面；
- 锁屏界面不再出现；
- 31317 执行期间界面或 SystemUI 异常。

旧版 doctor 存在漏掉提前返回的问题，可能误入 31317。它不会直接修改
PIN、图案或锁屏凭据，但异常的 Zygote/SystemUI 运行态需要安全清理并普通重启。

本包只支持以下 XPad2 /260 固件：

alps/vnd_ls12_mt8797_wifi_64/ls12_mt8797_wifi_64:13/TP1A.220624.014/260:user/release-keys

本包不包含 adb.exe
------------------

请任选一种方式：

1. 推荐：把本恢复包的全部文件解压到 Android platform-tools 文件夹，确保
   START-LOCKSCREEN-RECOVERY.bat 与 adb.exe 位于同一目录。
2. 或者让 adb.exe 已经可以从 Windows 的 PATH 中运行。

不要只在 ZIP 压缩包预览窗口中双击，必须先“全部解压”。

操作步骤
--------

1. 在平板上打开 USB 调试。
2. 用 USB 线连接电脑，只连接这一台需要恢复的平板。
3. 解锁平板；如果出现 USB 调试授权弹窗，点击“允许”。
4. 双击 START-LOCKSCREEN-RECOVERY.bat。
5. 阅读提示，按 Y 开始。
6. 等待平板自动普通重启。不要拔线，最长约 3 分钟。
7. 脚本会打开系统“安全”设置：
   - 只需要普通锁屏时选择“滑动”；
   - 需要 PIN 或图案时，由用户本人在设置中重新设置。
8. 按一次电源键熄屏，再唤醒确认锁屏是否恢复。

工具实际做什么
--------------

- 在任何修改前保存恢复前诊断；
- 校验设备必须是受支持的 /260 固件；
- 校验内置 xpad-install v0.2.2 的 SHA-256；
- 只运行新版的 self-test 和 cleanup，不运行 doctor，不进入新的 31317；
- 清理能够确认属于旧 31317 的残留 payload/listener；
- 仅在“没有任何锁屏凭据且锁屏服务明确被禁用”时，通过 Android 官方
  locksettings 服务恢复非安全滑动锁屏；
- 普通重启；
- 保存重启后的锁屏、Keyguard、核心进程和崩溃状态；
- 打开系统安全设置。

工具绝不会做什么
----------------

- 不会清除现有 PIN、图案或密码；
- 不会自动创建 PIN、图案或密码；
- 不会执行 locksettings clear、set-pin、set-pattern 或 set-password；
- 不会直接写 settings secure 的 lockscreen.disabled；
- 不会下载或内含 adb.exe；
- 不会在不支持的固件上继续执行。

诊断日志
--------

脚本会在当前目录生成：

xpad2-lockscreen-recovery-log-*.zip

其中包括：

- 恢复前后 Boot ID、SELinux、Zygote/system_server/SystemUI PID；
- LockSettingsService 和 Keyguard/Window policy 状态；
- hidden_api_blacklist_exemptions 的长度、SHA-256 和已知 payload 特征，
  但不保存设置原文；
- 8888 临时 listener 状态；
- 与 Zygote、SystemUI、Keyguard、31317 相关的过滤日志；
- system_server/system_app DropBox 崩溃记录；
- xpad-install self-test、cleanup 输出和退出码；
- 新版 31317 阶段日志（如果设备上存在）。

打包前会脱敏设备序列号。日志不会记录 PIN、图案或密码。如果恢复后仍然
异常，把这个 ZIP 发给维护者远程分析。

如果脚本报错
------------

- “找不到 adb.exe”：把恢复包解压到 platform-tools 目录。
- “平板尚未授权”：解锁平板并在 USB 调试弹窗点“允许”，再运行。
- “检测到多台设备”：拔掉其他 Android 设备。
- “固件不是受支持的 /260”：停止使用，不要绕过校验。
- “清理返回异常代码”：脚本不会自动重启，把诊断 ZIP 发给维护者。
- “3 分钟内没有重新连上”：清理和重启命令可能已经执行；等待平板启动，
  重新插拔 USB 后再检查。

版本与来源
----------

恢复清理器：xpad-installer v0.2.2
项目：https://github.com/yoyicue/xpad-installer
许可证：GPL-3.0-only，见 LICENSE。
