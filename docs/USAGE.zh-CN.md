# xpad-installer 小白使用手册

这份手册面向第一次接触 ADB 的用户。按顺序操作即可，不需要预先理解 Android 开发。

> **适用范围**
>
> xpad-installer 是针对特定 XPad2 固件验证的工具，只能用于你拥有或明确获准维护的设备。不要连接或操作无关设备，也不要把这里的命令照搬到其他型号。

## 1. 先分清三个名字

- **ADB**：运行在电脑上的 Android 调试工具。它负责发现设备、传文件、执行设备端命令。
- **xpad-installer**：本项目和 GitHub 仓库的名称。
- **xpad-install**：真正放到平板上运行的 ARM64 命令行文件。

因此，常见命令的结构是：

```text
电脑上的 adb  -s 设备序列号  shell  平板上的 xpad-install  子命令
```

例如：

```shell
adb -s SERIAL shell /data/local/tmp/xpad-install doctor
```

`SERIAL` 是占位符，必须替换为 `adb devices -l` 显示的真实序列号，不要原样照抄。

## 2. ADB 是什么

ADB 全称 Android Debug Bridge。它由三部分配合工作：

1. 电脑上的 `adb` 客户端；
2. 电脑后台的 ADB server；
3. Android 设备里的 `adbd`。

普通量产固件中，`adb shell` 通常是 UID 2000（`shell`），**不是 root**。可以这样确认：

```shell
adb -s SERIAL shell id
```

常见输出类似：

```text
uid=2000(shell) gid=2000(shell) ...
```

不要先执行 `adb root`。量产版 Android 一般不支持它，xpad-install 也不依赖它；工具会按固件情况选择经过约束的 UID 10072、临时 root RPC 或 UID 1000 通道。

## 3. 在电脑上安装 ADB

ADB 包含在 Android SDK Platform-Tools 中。请从 Android 官方页面下载最新稳定版：

- [Android SDK Platform-Tools](https://developer.android.com/tools/releases/platform-tools)
- [ADB 官方说明](https://developer.android.com/tools/adb)

### 3.1 Windows

1. 下载 Windows ZIP 并解压，例如放到 `C:\platform-tools`。
2. 打开 PowerShell。
3. 进入目录并检查：

```powershell
cd C:\platform-tools
.\adb.exe version
```

如果想在当前 PowerShell 窗口直接输入 `adb`：

```powershell
$env:Path += ";C:\platform-tools"
adb version
```

关闭这个 PowerShell 窗口后，临时 PATH 会失效。也可以一直使用 `.\adb.exe`。

### 3.2 macOS

下载 Mac ZIP、解压，然后在“终端”中执行：

```shell
cd ~/Downloads/platform-tools
./adb version
export PATH="$PWD:$PATH"
```

最后一行只对当前终端窗口生效。安装了 Android Studio 的用户也可以使用 Android SDK 中已有的 Platform-Tools。

### 3.3 Linux

下载 Linux ZIP、解压，然后执行：

```shell
cd ~/Downloads/platform-tools
./adb version
export PATH="$PWD:$PATH"
```

如果系统无法识别 USB 设备，可能还需要发行版提供的 Android udev 规则。不要从来路不明的网站安装“万能驱动”。

## 4. 在平板上打开 USB 调试

Android 官方流程是：

1. 打开“设置 → 关于设备”；
2. 连续点击“版本号/Build number”七次，启用开发者选项；
3. 返回设置，进入“开发者选项”；
4. 打开“USB 调试”。

不同 OEM 的菜单名称可能不同，参考 [Android 官方开发者选项说明](https://developer.android.com/studio/debug/dev-options)。

使用支持数据传输的 USB 线连接电脑。平板第一次连接这台电脑时通常会弹出 RSA 授权窗口：

- 核对是自己的电脑；
- 勾选“始终允许”是可选项；
- 点击“允许”。

## 5. 确认 ADB 连接

在电脑执行：

```shell
adb devices -l
```

状态含义：

| 显示 | 含义 | 处理 |
|---|---|---|
| `SERIAL device` | 已连接，可以继续 | 记录 SERIAL |
| `SERIAL unauthorized` | 平板尚未授权电脑 | 解锁屏幕并确认 RSA 弹窗 |
| `SERIAL offline` | 通道异常 | 重插数据线，再运行 `adb kill-server` 和 `adb start-server` |
| 列表为空 | 没识别到设备 | 换数据线/USB 口，检查 USB 调试与 Windows OEM 驱动 |
| 多台 `device` | 同时连接了多台设备 | 每条命令都必须带 `-s SERIAL` |

多设备环境中不要省略 `-s SERIAL`。省略后，ADB 可能报“more than one device/emulator”，也可能操作到错误设备。

可以做一个无害检查：

```shell
adb -s SERIAL get-state
adb -s SERIAL shell id
adb -s SERIAL shell getprop ro.product.model
```

## 6. 下载并校验 xpad-installer

从项目的 [GitHub Releases](https://github.com/yoyicue/xpad-installer/releases) 下载 Android ARM64 ZIP，解压后应看到：

- `xpad-install`
- `README.md`
- `USAGE.zh-CN.md`
- `LICENSE`
- `SHA256SUMS`

macOS/Linux 校验：

```shell
cd xpad-installer-v0.2.0-android-arm64
shasum -a 256 -c SHA256SUMS
```

应显示：

```text
xpad-install: OK
```

Windows PowerShell 校验：

```powershell
Get-FileHash .\xpad-install -Algorithm SHA256
Get-Content .\SHA256SUMS
```

比较两边的 SHA-256 是否完全一致。不要使用来源不明或校验不一致的二进制。

## 7. 把 xpad-install 放到平板

在解压目录执行：

```shell
adb -s SERIAL push xpad-install /data/local/tmp/xpad-install
adb -s SERIAL shell chmod 755 /data/local/tmp/xpad-install
adb -s SERIAL shell ls -l /data/local/tmp/xpad-install
```

显示权限中包含 `x`，例如 `-rwxr-xr-x`，说明可以执行。

查看帮助：

```shell
adb -s SERIAL shell /data/local/tmp/xpad-install --help
```

删除命令行工具本身时使用：

```shell
adb -s SERIAL shell rm -f /data/local/tmp/xpad-install
```

这只删除 CLI 文件，不会卸载已安装的 APK。

## 8. 三步快速安装 APK

### 第一步：诊断

```shell
adb -s SERIAL shell /data/local/tmp/xpad-install doctor
```

典型字段：

| 字段 | 含义 |
|---|---|
| `uid` | 本次受约束操作使用的 Android UID |
| `transport` | 选中的身份通道，例如 `0044`、`31317` 或临时 root |
| `selinux` | 当前进程的 SELinux 域 |
| `znxxservice` | OEM 安装服务是否存在 |
| `provider` | OEM Provider 后端是否可用 |
| `direct` | UID 1000 PackageInstaller 后端是否可用 |

`doctor` 主要用于诊断，但在尚未准备 UID 10072 或临时 root 通道时，可能短暂使用 31317 获得 UID 1000。工具正常退出时会自动清理。

### 第二步：把 APK 传到 Download

```shell
adb -s SERIAL push your-app.apk /sdcard/Download/your-app.apk
```

推荐使用 `/sdcard/Download`，因为 shell 和 OEM 安装通道都容易读取这里的文件。

目前只支持单个普通 `.apk`。不要直接传 `.apks`、`.xapk`、`.apkm` 或拆分 APK 集合。

### 第三步：安装并验证

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install install --backend auto /sdcard/Download/your-app.apk
```

成功日志通常包含：

```text
INSTALL SUCCESS
package=...
versionCode=...
installer=com.tal.pad.znxxservice
```

再按 APK 的真实包名验证：

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install verify com.example.app
```

不知道包名时，可以从安装日志的 `package=...` 读取。

完成后可删除 Download 中的 APK：

```shell
adb -s SERIAL shell rm -f /sdcard/Download/your-app.apk
```

## 9. xpad-install 命令详解

### 9.1 `--help`、`-h`、`help`

```shell
adb -s SERIAL shell /data/local/tmp/xpad-install --help
```

只打印帮助，不执行安装或提权流程。

### 9.2 `doctor`

```shell
adb -s SERIAL shell /data/local/tmp/xpad-install doctor
```

检查当前身份、SELinux 域、OEM `znxxservice` 和安装后端。它不会安装或卸载 APK。

注意：为完成 UID 1000 侧诊断，它可能创建一个短生命周期的 31317 runner；正常返回后会恢复隐藏设置并停止临时 listener。

### 9.3 `install`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install install --backend auto /sdcard/Download/app.apk
```

安装一个单 APK，并在结束时检查：

- 包是否存在；
- 已安装 versionCode 是否不低于 APK；
- installer 来源是否是 `com.tal.pad.znxxservice`。

参数顺序必须是：`install`、可选 `--backend 值`、最后是 APK 路径。

### 9.4 `upgrade`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install upgrade --backend auto /sdcard/Download/app-new.apk
```

`upgrade` 和 `install` 使用相同的底层事务。这个名字用于表达“目标包已经存在”的操作意图。

Android 仍会强制要求：

- 新旧 APK 包名一致；
- 签名证书兼容；
- versionCode 不低于已安装版本；
- APK 本身有效。

它不会自动绕过签名不一致或版本降级保护。

### 9.5 `--backend auto|provider|direct`

| 后端 | 含义 | 适合谁 |
|---|---|---|
| `auto` | 优先尝试 OEM/0044 路径；失败时选择受支持的安全回退 | 小白首选 |
| `provider` | 只请求真实 `znxxservice` Provider 安装 | 已确认 Provider 可用时 |
| `direct` | 以 UID 1000 创建 PackageInstaller session，并通过 FileBridge 写入 APK | Provider 不可用的诊断/回退 |

建议始终先用 `auto`。

`direct` 不是“root 强装”。源码明确阻止 UID 0 直接调用该后端；它需要正确的 UID 1000 身份和 OEM installer attribution。

### 9.6 `verify PACKAGE [VERSION_CODE]`

只检查，不安装：

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install verify com.example.app
```

指定最低 versionCode：

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install verify com.example.app 123
```

成功条件同时包括：

1. 包已安装；
2. 已安装 versionCode 大于或等于参数；省略时按 0；
3. installer 来源是 `com.tal.pad.znxxservice`。

所以“应用明明存在但 verify 返回非 0”不一定是故障，也可能是应用由其他安装器装入。

### 9.7 `cleanup`

```shell
adb -s SERIAL shell /data/local/tmp/xpad-install cleanup
```

用于请求清理正常事务涉及的内嵌 DEX、临时 transfer 文件/listener 和 31317 隐藏设置。

它不会：

- 卸载已经安装的应用；
- 删除 `/data/local/tmp/xpad-install` 自身；
- 清空整个 `/data/local/tmp`；
- 卸载已经在内核中的 KernelSU 模块。

如果进程曾被强制杀死、zygote/system_server PID 发生变化，或者连续重试仍失败，应停止操作，由设备所有者执行一次普通重启后再检查，不要无限重复 31317。

### 9.8 `activate`

用于 BoomInstaller 已经安装后的“一次性激活 + 后续开机配置”：

```shell
adb -s SERIAL shell '
APK=$(pm path com.yoyicue.boominstaller)
APK=${APK#package:}
STARTER=${APK%/base.apk}/lib/arm64/libshizuku.so
/data/local/tmp/xpad-install activate --starter="$STARTER" --apk="$APK"
'
```

注意参数采用等号形式：

- `--starter=/绝对路径/libshizuku.so`
- `--apk=/绝对路径/base.apk`

不要写成 `--starter 路径`。

`activate` 会依次：

1. 给 BoomInstaller 配置所需权限与无线 ADB 设置；
2. 通过 Android 标准 TLS pairing 配对 BoomInstaller 自己的本地 ADB key；
3. 等待 OEM pairing 服务完成收尾并刷新无线 ADB；
4. 选择 UID 10072、临时 root 或 UID 1000；
5. 启动 BoomInstaller 服务。

它会改变设备的无线 ADB 全局设置。只对已经安装并信任的 BoomInstaller 使用。

### 9.9 `autostart enable`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install autostart enable
```

只配置 BoomInstaller 以后普通开机使用的本地无线 ADB 配对，不负责替换当前正在运行的服务。

适合当前服务已经启动，只想重新配置持久化时使用。首次部署通常直接执行 `activate` 即可。

### 9.10 `znxrun status`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install znxrun status
```

这是纯只读健康检查，不启动 31317，也不修改包状态。输出只有四种状态：

| 状态 | 含义 | 建议 |
|---|---|---|
| `healthy` | alias 为 UID 10072，正式 anchor attribution 也完整 | 无需操作 |
| `legacy` | alias 当前可用，但没有正式 anchor 作为持久化来源 | 执行一次 `znxrun ensure` |
| `missing` | 当前没有可用 alias | 执行一次 `znxrun ensure` |
| `invalid` | alias 存在但身份不是预期 UID 10072 | 停止安装并保留日志 |

健康输出示例：

```text
ZNXRUN_STATUS status=healthy alias=healthy uid=10072 anchor=anchored package=com.yoyicue.xpad2.installeranchor
```

### 9.11 `znxrun ensure`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install znxrun ensure
```

这是普通维护应使用的幂等命令，不需要另找 APK：

1. 健康时立即返回，不创建安装事务；
2. 缺失时从 `xpad-install` 自身取出正式签名的无代码、无权限 anchor；
3. 临时把 anchor 加入 OEM 安装白名单；
4. 必要时先安装 anchor，再通过继承更新保存 0044 attribution；
5. 无论成功失败都恢复原白名单；收到中断信号时由父进程负责恢复并清理临时 runner；
6. 最终同时验证 attribution 和 `run-as znxrun` 的 UID，才报告成功。

工具不会直接修改 `/data/system/packages.list`。持久化信息保存在 Android 的包元数据中，
以后 PackageManager 正常重写 `packages.list` 时会重新生成 alias。

`install --backend auto` 和 `upgrade --backend auto` 也会在需要时自动执行同样的修复。

### 9.12 `znxrun preflight`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install znxrun preflight
```

高级诊断命令。它计算并打印 UID 10072 的 0044 alias 信息，默认不创建 alias。

虽然它不做持久化 alias 写入，但为了从 UID 1000 读取必要信息，仍可能短暂使用 31317。

### 9.13 `znxrun create --package PACKAGE --apk UPDATE.apk [--apply]`

这是保留给开发者验证非托管 carrier 的底层命令。日常恢复只使用 `znxrun ensure`。

先做 dry-run：

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install znxrun create \
  --package com.example.carrier \
  --apk /sdcard/Download/signed-update.apk
```

不带 `--apply` 时只检查：

- APK 是否有效；
- 对应包是否已经安装；
- APK 包名是否对应已安装包；
- APK versionCode 是否为等版或新版；
- 将要使用的 alias 数据。

dry-run 不会证明签名一定兼容；签名证书仍由 Android 在真正提交安装时校验。

真正应用：

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install znxrun create \
  --package com.example.carrier \
  --apk /sdcard/Download/signed-update.apk --apply
```

`--package` 必须与 APK 的真实包名一致。`--apply` 会调用设备特定的 CVE-2024-0044
路径，临时修改安装白名单，并通过 PackageInstaller + FileBridge 提交继承更新。
外层监督进程负责恢复白名单；Android 负责在失败时回收/放弃未完成 session。

这是初始化/恢复 UID 10072 alias 的高级命令。小白不要执行 `--apply`，除非维护文档明确要求，并且已经备份现场状态。

### 9.14 内部命令：`serve` 和 `--root-child`

源码还包含 `serve` 和 `--root-child`，它们是 `activate` 及临时 root RPC 内部调用的入口：

- `serve` 只允许 UID 0、1000 或 10072；
- `--root-child` 用于受约束的临时 root transport。

它们不是普通用户命令，不要手工调用。

## 10. 身份通道是什么意思

xpad-install 会按环境自动选择：

| 通道 | 身份 | 用途 |
|---|---:|---|
| `0044 run-as znxrun` | UID 10072 | 优先使用 OEM 安装身份 |
| `temp_su.sock` | 临时 UID 0 RPC，再降为 UID 1000 | 已有临时引导窗口时使用 |
| `31317 system runner` | UID 1000 / `system_app` | 当前固件的安全回退 |

这些通道不是给用户提供通用 shell。工具只执行固定安装、验证、清理与 BoomInstaller 启动操作。

31317 正常流程会：

- 对齐 64/32 位 Zygote；
- 使用两个牺牲 activity；
- 删除 `hidden_api_blacklist_exemptions`；
- 停止临时 listener 和 runner；
- 删除 Settings 私有缓存里的临时 DEX/APK。

## 11. 如何看日志和退出码

日志前缀：

| 前缀 | 含义 |
|---|---|
| `[*]` | 正在执行或状态信息 |
| `[+]` | 步骤成功 |
| `[!]` | 警告，可能已自动回退 |
| `[-]` | 操作失败 |

macOS/Linux 查看上一条命令退出码：

```shell
echo $?
```

PowerShell 查看上一条 `adb.exe` 退出码：

```powershell
$LASTEXITCODE
```

常见退出码：

| 退出码 | 含义 |
|---:|---|
| 0 | 成功 |
| 1 | 安装、回调或最终验证失败 |
| 2/64 | 参数或命令格式错误 |
| 66 | starter/APK 路径不存在或不可读/不可执行 |
| 70 | 内存、fork 或内部执行准备失败 |
| 74 | DEX/APK 写入或传输失败 |
| 77 | 身份、权限、配对或系统配置不满足 |
| 124 | RPC/操作超时 |
| 125 | 临时 root RPC 没收到完整返回标记 |
| 126/127 | starter/系统命令无法执行或 transport 不可用 |

非 0 时先保存完整输出，不要只截最后一行。

## 12. 常见问题排查

### `adb: command not found`

还没有进入 Platform-Tools 目录，或 PATH 没配置。使用完整路径：

- macOS/Linux：`./adb`
- Windows PowerShell：`.\adb.exe`

### `unauthorized`

解锁平板，确认 RSA 授权弹窗。没有弹窗时：

1. 断开 USB；
2. 在开发者选项中撤销 USB 调试授权；
3. 重新连接并确认。

这会影响所有已授权电脑，操作前先确认。

### `offline`

```shell
adb kill-server
adb start-server
adb devices -l
```

仍失败时更换数据线或 USB 口。

### `more than one device/emulator`

命令缺少 `-s SERIAL`。从 `adb devices -l` 复制正确序列号。

### `APK unreadable or empty` / `invalid APK`

确认：

- 文件确实是普通单 APK；
- 设备路径拼写和大小写正确；
- 已成功 push 到 `/sdcard/Download`；
- 文件大小不是 0。

```shell
adb -s SERIAL shell ls -lh /sdcard/Download/your-app.apk
```

### `INSTALL_FAILED_UPDATE_INCOMPATIBLE`

已安装包与新 APK 签名不兼容。不能靠换 backend 绕过。必须使用相同签名的更新，或者在明确接受数据丢失后先卸载旧包。

### `INSTALL_FAILED_VERSION_DOWNGRADE`

新 APK 的 versionCode 更低。使用更高版本，或重新构建正确 versionCode；工具不会默认绕过降级保护。

### `uid 10072 cannot deliver Binder ... using uid 1000`

这是已知固件上的正常回退提示，不等于失败。继续看最终是否出现 `INSTALL SUCCESS` 或服务 PID。

### `zygote alignment primary=0 secondary=0`

两个触发 activity 都正常完成，通常是预期诊断信息。是否成功仍以最终退出码和后续日志为准。

### 设备核心进程变化或环境疑似脏了

先记录：

```shell
adb -s SERIAL shell 'pidof zygote64; pidof zygote; pidof system_server; getenforce'
```

如果 zygote/system_server 异常重启、隐藏设置没有恢复、操作连续失败或设备行为异常：

1. 停止继续安装；
2. 不要反复触发；
3. 保存完整日志；
4. 由设备所有者进行一次普通重启；
5. 重启稳定后重新运行 `doctor`。

工具和自动化脚本不应自行重启设备。

## 13. 常用 ADB 命令速查

| 命令 | 作用 |
|---|---|
| `adb version` | 查看 ADB 版本 |
| `adb devices -l` | 列出设备和序列号 |
| `adb -s SERIAL get-state` | 检查指定设备状态 |
| `adb -s SERIAL shell id` | 查看 ADB shell 身份 |
| `adb -s SERIAL shell COMMAND` | 在设备执行一条命令 |
| `adb -s SERIAL push LOCAL REMOTE` | 从电脑传文件到设备 |
| `adb -s SERIAL pull REMOTE LOCAL` | 从设备取文件到电脑 |
| `adb -s SERIAL shell pm path PACKAGE` | 查看已安装 APK 路径 |
| `adb -s SERIAL shell pidof PROCESS` | 查看进程 PID |
| `adb kill-server` | 停止电脑端 ADB server |
| `adb start-server` | 启动电脑端 ADB server |

路径中有空格时，应在电脑端给整个路径加引号。

## 14. 安全习惯

- 每条命令都显式写 `-s SERIAL`。
- 一次只运行一个 xpad-install 操作。
- 安装/31317 运行中不要拔线或强制结束进程。
- 不要把 ADB 暴露在公共网络。
- 不要从未知来源下载 APK 或 xpad-install。
- 日常维护只使用幂等的 `znxrun ensure`；不要把底层 `znxrun create --apply` 当成普通安装命令。
- 不要在无关设备上试运行。
- 操作前记录 boot ID、zygote 和 system_server PID，便于判断现场是否稳定。

## 15. 官方参考资料

- [Android Debug Bridge (adb)](https://developer.android.com/tools/adb)
- [SDK Platform-Tools 下载与发布说明](https://developer.android.com/tools/releases/platform-tools)
- [启用 Android 开发者选项和 USB 调试](https://developer.android.com/studio/debug/dev-options)
- [在真实 Android 设备上运行和调试](https://developer.android.com/studio/run/device)
