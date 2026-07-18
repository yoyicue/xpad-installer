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

不要先执行 `adb root`。量产版 Android 一般不支持它，xpad-install 也不依赖它。目标 APK 始终由受管的 0044 身份安装；该身份的 UID 必须等于本机 `com.tal.pad.znxxservice` 的真实 UID，而不是固定数字。只有缺失或损坏 0044 时，工具才短暂使用 UID 1000/31317 修复它。

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
cd xpad-installer-v0.2.11-android-arm64
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
| `XPAD_INSTALL_SELF_TEST` | ELF 内嵌 DEX、正式 anchor 和构建版本是否自检通过 |
| `uid` | 当前只读诊断进程的 Android UID |
| `transport` | 固定为 `none`，表示没有选择身份通道 |
| `selinux` | 当前进程的 SELinux 域 |
| `provider` | OEM Provider 后端是否可用 |
| `ZNXRUN_STATUS` | 0044 备用身份当前状态 |
| `31317` | 固定为 `not-probed` |

`doctor`、`verify`、`cleanup` 和 `znxrun status` 都在 native 层提前返回，不会进入 31317。`self-test` 更窄，只读取 ELF 内嵌内容，供 `xpad2 status/verify` 使用。

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

检查当前身份、SELinux 域、OEM `znxxservice`、0044 状态和 ELF 内嵌内容。它不会安装或卸载 APK。

这是只读 native 诊断：不选择 transport、不写系统设置，也不会创建 31317 runner。

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
| `auto` | 在 0044 身份中优先请求 OEM Provider，未提交时再用同一身份的 PackageInstaller | 小白首选 |
| `provider` | 在 0044 身份中只请求真实 `znxxservice` Provider 安装 | 已确认 Provider 可用时 |
| `direct` | 在 0044 身份中创建 PackageInstaller session，并通过 FileBridge 写入 APK | Provider 不可用时诊断 |

建议始终先用 `auto`。

如果安装前没有健康的 0044，31317 只负责补回正式 anchor 和 alias。外层再次验证 alias
UID 与本机 OEM installer UID 一致后才会把目标 APK 交给 0044；补回失败则直接返回失败，目标 APK 不会经 31317 提交。
安装过程中若 0044 意外变坏，工具只修复一次并重新走一次 0044。

`direct` 不是“root 强装”，它仍需要健康的 0044/OEM installer 身份和 attribution。

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

用于请求清理正常事务涉及的内嵌 DEX、临时 transfer 文件/listener，并优先从持久备份精确恢复 31317 之前的隐藏设置。没有备份时，它只会删除能够确认属于本工具的残留 payload，不会覆盖无关设置值。

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
2. 首次使用时等待你在 Pad 上确认“始终允许使用此网络”，并确认 TLS 端口连续稳定；
3. 通过 Android 标准 TLS pairing 配对 BoomInstaller 自己的本地 ADB key；
4. 直接使用当前 root 或标准 ADB shell 身份启动 BoomInstaller 服务。

`activate` 不会探测或进入 0044/31317。BoomInstaller 的 Shizuku 服务属于控制面，
保持标准 root/shell 模型；0044 属于 APK 安装数据面，受保护的 31317 只负责修复 0044。这样普通开机
自启动不会因为启动 BoomInstaller 而触发 31317。

它会改变设备的无线 ADB 全局设置。只对已经安装并信任的 BoomInstaller 使用。

### 9.9 `autostart enable`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install autostart enable
```

只配置 BoomInstaller 以后普通开机使用的本地无线 ADB 配对，不负责替换当前正在运行的服务。

适合当前服务已经启动，只想重新配置持久化时使用。首次部署通常直接执行 `activate` 即可。

首次配置最长等待 90 秒。出现无线调试授权弹窗时，必须勾选“始终允许使用此网络”后确认；
未确认、系统把 `adb_wifi_enabled` 回滚为 0、或 TLS 端口未稳定时命令会以非零退出，
不会输出假成功。配对成功会输出 `autostart=paired` 和
`autostart_reboot=pending`，表示需要一次普通重启验收自动启动。

### 9.10 `autostart status`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install autostart status
```

只读输出 Boom Provider 是否可调用、配对密钥是否存在且可解密、配对状态、最近一次
启动状态、运行模式与服务 UID。它不会打开无线调试、重新配对或进入 0044/31317。

### 9.11 `znxrun status`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install znxrun status
```

这是纯只读健康检查，不启动 31317，也不修改包状态。输出只有四种状态：

| 状态 | 含义 | 建议 |
|---|---|---|
| `healthy` | alias UID 与本机 OEM installer UID 一致，正式 anchor attribution 也完整 | 无需操作 |
| `legacy` | alias 当前可用，但没有正式 anchor 作为持久化来源 | 执行一次 `znxrun ensure` |
| `missing` | 当前没有可用 alias | 执行一次 `znxrun ensure` |
| `invalid` | alias 存在但身份与本机 OEM installer UID 不一致 | 停止安装并保留日志 |

健康输出示例：

```text
ZNXRUN_STATUS status=healthy alias=healthy uid=10070 expected_uid=10070 anchor=anchored package=com.yoyicue.xpad2.installeranchor
```

### 9.12 `znxrun ensure`

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
6. 最终同时验证 attribution、`run-as znxrun` UID 和 PackageManager 报告的本机 OEM installer UID 三者一致，才报告成功。

工具不会直接修改 `/data/system/packages.list`。持久化信息保存在 Android 的包元数据中，
以后 PackageManager 正常重写 `packages.list` 时会重新生成 alias。

从 v0.2.8 起，正式 anchor 使用 versionCode 2。这样遇到“attribution 已持久化、alias
尚未物化”的现场时，修复会用 full update 形成一次真实升级；同版本修复仍使用
inherit-existing。提交后最多按秒轮询 60 秒，同时验证 alias、实际 UID 和 attribution，
并每五秒记录一次等待进度；
健康后立即结束，不固定等待。

`install --backend auto` 和 `upgrade --backend auto` 也会在需要时自动执行同样的修复。

### 9.13 `znxrun preflight`

```shell
adb -s SERIAL shell \
  /data/local/tmp/xpad-install znxrun preflight
```

高级诊断命令。它读取本机 OEM installer 的真实 UID，计算并打印对应的 0044 alias 信息，默认不创建 alias。不同设备可能显示 10070、10072 等不同值。

虽然它不做持久化 alias 写入，但为了从 UID 1000 读取必要信息，仍可能短暂使用 31317。

### 9.14 `znxrun create --package PACKAGE --apk UPDATE.apk [--apply]`

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

这是初始化/恢复设备专属 OEM installer alias 的高级命令。小白不要执行 `--apply`，除非维护文档明确要求，并且已经备份现场状态。

### 9.15 内部命令：`serve` 和 `--root-child`

源码还包含 `serve` 和 `--root-child`，它们是内部调用入口：

- `serve` 只允许 UID 0 或标准 ADB shell UID 2000；
- `--root-child` 用于受约束的临时 root transport。

它们不是普通用户命令，不要手工调用。

## 10. 身份通道是什么意思

xpad-install 将“安装身份”和“修复手段”严格分开：

| 通道 | 身份 | 用途 |
|---|---:|---|
| `0044 run-as znxrun` | 与本机 `com.tal.pad.znxxservice` UID 一致 | 唯一的目标 APK 安装身份 |
| `temp_su.sock` | 临时 UID 0 RPC | 仅供明确的内部/维护命令，不安装目标 APK |
| `31317 system runner` | UID 1000 / `system_app` | 仅在 0044 缺失或失效时补建/修复 0044 |

这些安装通道不是给用户提供通用 shell，只执行固定安装、验证与清理操作。
BoomInstaller 启动不使用这里的 0044/31317 身份选择。

31317 修复流程会：

- 对齐 64/32 位 Zygote；
- 使用两个牺牲 activity；
- 在写入前持久保存 `hidden_api_blacklist_exemptions` 的原始缺失/存在状态和完整值；
- 每个阶段记录 boot ID、Zygote、system_server、SystemUI PID 和设置元数据；
- 精确恢复并复核原值；
- 停止临时 listener 和 runner；
- 删除 Settings 私有缓存里的临时 DEX/APK。

阶段日志保存在 `/data/local/tmp/.xpad-installer/logs/31317-*.jsonl`。任何核心 PID 变化都会立即停止后续尝试、写入本启动周期熔断标记并返回 75；普通重启前不会再次进入 31317。

## 11. 如何看日志和退出码

日志前缀：

| 前缀 | 含义 |
|---|---|
| `[*]` | 正在执行或状态信息 |
| `[+]` | 步骤成功 |
| `[!]` | 警告，可能触发了有界的 0044 修复 |
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
| 75 | 核心 PID/boot 状态变化或精确恢复失败；本启动周期禁止继续 31317，必须普通重启 |
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

### `managed 0044 is unavailable; repairing it before installation`

工具尚未开始安装目标 APK，正在用有界的 31317 事务补回 0044。只有随后出现健康的
`ZNXRUN_STATUS`，目标 APK 才会经 0044 提交；修复失败时不要连续重试，按退出码决定是否普通重启。

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

### 旧版 `doctor` 后锁屏或 SystemUI 异常

v0.2.1 及更早版本的 `doctor` 没有在 native 诊断后提前返回，可能误入 31317。
旧代码不直接修改 PIN、图案或锁屏凭据；如果随后出现按电源键唤醒后直接回到
桌面、锁屏界面消失或 SystemUI 异常，应停止运行旧版命令。

面向不会使用 ADB 的用户，可使用 Release 中的
`xpad-installer-v0.2.2-windows-lockscreen-recovery.zip`：完整解压到已有的
Android Platform-Tools 目录，双击 `START-LOCKSCREEN-RECOVERY.bat`。恢复包不
内含或下载 `adb.exe`。

批处理会在修改前后保存锁屏服务、Keyguard、核心 PID、31317 残留特征、过滤
日志和 DropBox 崩溃记录，脱敏序列号后生成 `xpad2-lockscreen-recovery-log-*.zip`。
它只运行 v0.2.2 的 `self-test`/`cleanup`，随后执行一次明确告知用户的普通重启；
不会清除或创建 PIN、图案、密码，也不会直接写
`settings secure lockscreen.disabled`。重启后仍需由用户在系统“安全”设置中
选择滑动、PIN 或图案。

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
