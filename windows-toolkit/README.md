# xpad 安全安装工具 v2.8.0

面向 Windows 小白用户的 GUI 安装器，仅用于本人拥有或已获得授权的 XPad2 设备。

## 使用

1. 安装 Python 3.10 或更高版本。
2. 将 `adb.exe` 及其 DLL 放在本目录、`platform-tools` 子目录或系统 `PATH` 中。
3. 双击 `xpad-safe-install-gui.bat`。
4. 选择设备 SN 和 APK，点击“安装”。

工具包不内置 `adb.exe`。设备端安装引擎固定为 `xpad-install` v0.2.8，并在运行前校验
SHA-256。程序会保存完整时间戳日志，但不在固定的 `doctor` 后空等 10 秒，也不会在
`auto` 已经回退 direct 后再次重复安装。

v0.2.8 使用 versionCode 2 的正式 installer anchor；版本推进使用 full update，
同版本修复使用 inherit-existing，并在 0044 提交后最多按秒轮询 60 秒。这样
`anchor=anchored` 但 alias 尚未物化时，不会只检查一次就误判失败。

无论成功或失败都会尝试执行 cleanup。错误会分类处理：

- `-126` / `INSTALL_FAILED_DUPLICATE_PERMISSION`：APK 全局权限所有权冲突，普通重启
  不能修复 APK；BoomInstaller 应使用 r14 或更高版本。
- exit 75 / `process is bad`：当前 boot 安全熔断，明确提示普通重启。
- 签名冲突及其他失败：保留原始错误并要求提供日志，不再统一误报“请重启”。

日志文件保存在工具目录，名称为 `xpad-safe-install-YYYYMMDD_HHMMSS.log`。
