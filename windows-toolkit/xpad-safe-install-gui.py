#!/usr/bin/env python3
# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

from __future__ import annotations

import datetime as dt
import hashlib
import os
import platform
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import traceback
from dataclasses import dataclass
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except ModuleNotFoundError:  # Pure classification tests may run without Tk installed.
    tk = None
    filedialog = None
    messagebox = None
    ttk = None

VERSION = "2.5.1"
BASE = Path(__file__).resolve().parent
TOOL_SHA256 = "cd21bb7b4df089361e45b72096e11aa639ec57675e6012097341dfc14ee9f388"
REMOTE_TOOL = "/data/local/tmp/xpad-install"
REMOTE_APK = "/data/local/tmp/xpad-target.apk"
PACKAGE_RE = re.compile(r"package=([A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+)")


class InstallError(RuntimeError):
    def __init__(self, message: str, reboot_required: bool = False):
        super().__init__(message)
        self.reboot_required = reboot_required


@dataclass(frozen=True)
class FailureAdvice:
    kind: str
    message: str
    reboot_required: bool


def classify_install_failure(output: str, exit_code: int) -> FailureAdvice:
    lower = output.casefold()
    if (
        "status=1: -126:" in lower
        or "install_failed_duplicate_permission" in lower
        or "redeclare permission group" in lower
        or ("permission group" in lower and "already owned by" in lower)
    ):
        return FailureAdvice(
            "permission-owner-conflict",
            "APK 声明的 Android 全局权限已被另一应用拥有。重启通常不能修复 APK 本身；"
            "BoomInstaller 请使用 r14 或更高版本。若刚卸载旧 owner，旧 APK 仍可能要等"
            "普通重启刷新 PackageManager，但新版 BoomInstaller 不需要抢占该权限。",
            False,
        )
    if exit_code == 75 or "process is bad" in lower or "bad process" in lower:
        return FailureAdvice(
            "reboot-required",
            "当前启动周期已触发安全熔断，请普通重启设备后再试。",
            True,
        )
    if "signature" in lower and ("mismatch" in lower or "incompatible" in lower):
        return FailureAdvice(
            "signature-conflict",
            "已安装应用与目标 APK 签名不兼容；重启无效，请核对 APK 来源和签名。",
            False,
        )
    return FailureAdvice(
        "install-failed",
        f"安装命令失败（exit={exit_code}）。请发送本次日志；未发现需要重启的安全信号。",
        False,
    )


class Log:
    def __init__(self, sink):
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        self.path = BASE / f"xpad-safe-install-{stamp}.log"
        self.file = self.path.open("w", encoding="utf-8")
        self.sink = sink
        self.lock = threading.Lock()

    def write(self, message: str = "") -> None:
        now = dt.datetime.now().astimezone().isoformat(timespec="milliseconds")
        line = f"[{now}] {message}"
        with self.lock:
            self.file.write(line + "\n")
            self.file.flush()
        self.sink(line)

    def close(self) -> None:
        with self.lock:
            if not self.file.closed:
                self.file.close()


def run(log: Log, args, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    command = [str(value) for value in args]
    started = time.monotonic()
    log.write(f"[CMD] {subprocess.list2cmdline(command)}  (timeout={timeout}s)")
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            shell=False,
            creationflags=creationflags,
        )
    except subprocess.TimeoutExpired as exc:
        log.write(f"[TIMEOUT] {time.monotonic() - started:.2f}s")
        raise InstallError(f"命令超过 {timeout} 秒") from exc
    for output, prefix in ((result.stdout, "  "), (result.stderr, "  ! ")):
        for line in output.splitlines():
            log.write(prefix + line)
    log.write(f"[END] exit={result.returncode}  {time.monotonic() - started:.2f}s")
    return result


def require(result: subprocess.CompletedProcess[str], action: str) -> None:
    if result.returncode:
        raise InstallError(f"{action}失败（exit={result.returncode}）")


def verify_tool(log: Log) -> Path:
    tool = BASE / "xpad-install"
    if not tool.is_file():
        raise InstallError("工具目录缺少 xpad-install")
    digest = hashlib.sha256(tool.read_bytes()).hexdigest()
    log.write(f"xpad-install SHA-256: {digest}")
    if digest != TOOL_SHA256:
        raise InstallError("xpad-install 哈希不匹配，拒绝运行")
    return tool


def find_adb(log: Log) -> Path:
    candidates = [BASE / "adb.exe", BASE / "platform-tools" / "adb.exe"]
    if found := shutil.which("adb"):
        candidates.append(Path(found))
    for adb in candidates:
        try:
            if run(log, [adb, "version"], 10).returncode == 0:
                return adb
        except OSError:
            continue
    raise InstallError("未找到可用 adb.exe；请放在工具目录、platform-tools 或 PATH")


def list_devices(log: Log, adb: Path) -> list[str]:
    result = run(log, [adb, "devices", "-l"], 20)
    require(result, "读取设备")
    devices = []
    for line in result.stdout.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            devices.append(fields[0])
    return devices


def package_name(result: subprocess.CompletedProcess[str]) -> str | None:
    match = PACKAGE_RE.search(result.stdout + "\n" + result.stderr)
    return match.group(1) if match else None


def package_exists(log: Log, adb: Path, serial: str, package: str) -> bool:
    result = run(log, [adb, "-s", serial, "shell", "pm", "path", package], 30)
    return result.returncode == 0 and "package:" in result.stdout


def pids(log: Log, adb: Path, serial: str) -> tuple[str, ...] | None:
    result = run(
        log,
        [adb, "-s", serial, "shell", "pidof zygote64; pidof zygote; pidof system_server"],
        20,
    )
    if result.returncode:
        return None
    values = tuple(line.strip() for line in result.stdout.splitlines() if line.strip())
    return values or None


def cleanup(log: Log, adb: Path | None, serial: str | None) -> None:
    if not adb or not serial:
        return
    log.write("[CLEANUP] 清理安装临时状态…")
    try:
        result = run(log, [adb, "-s", serial, "shell", REMOTE_TOOL, "cleanup"], 180)
        if result.returncode:
            log.write(f"[WARN] cleanup exit={result.returncode}")
        run(log, [adb, "-s", serial, "shell", "rm", "-f", REMOTE_APK], 30)
    except Exception as exc:  # cleanup must not hide the original failure
        log.write(f"[WARN] cleanup异常：{type(exc).__name__}: {exc}")


def failure_snapshot(log: Log, adb: Path | None, serial: str | None, stage: str) -> None:
    log.write(f"[DIAG] 失败快照：{stage}")
    if not adb:
        return
    commands = [[adb, "devices", "-l"]]
    if serial:
        commands.extend(
            [
                [adb, "-s", serial, "get-state"],
                [adb, "-s", serial, "shell", "getprop", "sys.boot_completed"],
            ]
        )
    for command in commands:
        try:
            run(log, command, 15)
        except Exception as exc:
            log.write(f"[DIAG-WARN] {type(exc).__name__}: {exc}")


def install(log: Log, serial: str, apk: Path) -> tuple[bool, str, bool]:
    stage = "startup"
    adb: Path | None = None
    remote_prepared = False
    try:
        log.write(f"xpad safe install tool v{VERSION}  (GUI)")
        log.write(f"Python: {sys.version.replace(chr(10), ' ')}")
        log.write(f"Platform: {platform.platform()}; pid={os.getpid()}")
        stage = "verify-tool"
        tool = verify_tool(log)
        stage = "find-adb"
        adb = find_adb(log)
        if serial not in list_devices(log, adb):
            raise InstallError(f"设备不可用或未授权：{serial}")
        if not apk.is_file() or apk.suffix.casefold() != ".apk":
            raise InstallError("请选择存在的 APK 文件")
        log.write(f"设备: {serial}  APK: {apk.name}")
        stage = "pre-install-pids"
        before = pids(log, adb, serial)
        stage = "push-tool"
        require(run(log, [adb, "-s", serial, "push", tool, REMOTE_TOOL], 120), "推送工具")
        require(
            run(log, [adb, "-s", serial, "shell", "chmod", "755", REMOTE_TOOL], 30),
            "设置工具权限",
        )
        remote_prepared = True
        stage = "doctor"
        require(run(log, [adb, "-s", serial, "shell", REMOTE_TOOL, "doctor"], 180), "doctor")
        log.write("doctor 通过；直接进入安装，不做固定等待")
        stage = "push-apk"
        require(run(log, [adb, "-s", serial, "push", apk, REMOTE_APK], 300), "推送 APK")
        stage = "install"
        log.write("自动确认 YES")
        result = run(
            log,
            [adb, "-s", serial, "shell", REMOTE_TOOL, "install", "--backend", "auto", REMOTE_APK],
            360,
        )
        package = package_name(result)
        log.write(f"安装 exit={result.returncode}  package={package or '<none>'}")
        if result.returncode:
            advice = classify_install_failure(result.stdout + "\n" + result.stderr, result.returncode)
            log.write(f"[CLASSIFY] kind={advice.kind} reboot={advice.reboot_required}")
            raise InstallError(advice.message, advice.reboot_required)
        if not package:
            raise InstallError("安装命令成功但无法解析包名；请发送日志，不要盲目重启")
        stage = "post-install-pids"
        after = pids(log, adb, serial)
        attempts = 10 if before is None or after is None or before != after else 2
        for attempt in range(1, attempts + 1):
            stage = f"verify-package-{attempt}/{attempts}"
            if package_exists(log, adb, serial, package):
                log.write(f"安装并验证成功：{package}")
                return True, f"安装成功：{package}", False
            if attempt < attempts:
                time.sleep(2)
        raise InstallError(f"PackageManager 未发现 {package}；请发送日志")
    except Exception as exc:
        log.write(f"[ERROR] stage={stage} {type(exc).__name__}: {exc}")
        log.write("[TRACEBACK]\n" + traceback.format_exc())
        failure_snapshot(log, adb, serial, stage)
        reboot = isinstance(exc, InstallError) and exc.reboot_required
        return False, str(exc), reboot
    finally:
        if remote_prepared:
            cleanup(log, adb, serial)


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title(f"xpad safe install tool v{VERSION}")
        self.root.geometry("860x620")
        self.messages: queue.Queue[tuple[str, object]] = queue.Queue()
        self.running = False
        self.device = tk.StringVar()
        self.apk = tk.StringVar()
        self.status = tk.StringVar(value="请选择设备和 APK")
        self._build()
        self.root.after(80, self._drain)
        self._refresh_async()

    def _build(self) -> None:
        frame = ttk.Frame(self.root, padding=12)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="设备 SN").grid(row=0, column=0, sticky="w")
        self.devices = ttk.Combobox(frame, textvariable=self.device, state="readonly", width=48)
        self.devices.grid(row=0, column=1, sticky="ew", padx=8)
        self.refresh = ttk.Button(frame, text="刷新", command=self._refresh_async)
        self.refresh.grid(row=0, column=2)
        ttk.Label(frame, text="APK").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(frame, textvariable=self.apk).grid(row=1, column=1, sticky="ew", padx=8, pady=(8, 0))
        ttk.Button(frame, text="选择…", command=self._choose_apk).grid(row=1, column=2, pady=(8, 0))
        self.start = ttk.Button(frame, text="安装", command=self._start)
        self.start.grid(row=2, column=0, columnspan=3, sticky="ew", pady=10)
        ttk.Label(frame, textvariable=self.status).grid(row=3, column=0, columnspan=3, sticky="w")
        self.log = tk.Text(frame, wrap="word", state="disabled", font=("Consolas", 10))
        self.log.grid(row=4, column=0, columnspan=3, sticky="nsew", pady=(8, 0))
        scroll = ttk.Scrollbar(frame, orient="vertical", command=self.log.yview)
        scroll.grid(row=4, column=3, sticky="ns", pady=(8, 0))
        self.log.configure(yscrollcommand=scroll.set)
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(4, weight=1)

    def _append(self, line: str) -> None:
        self.messages.put(("log", line))

    def _choose_apk(self) -> None:
        selected = filedialog.askopenfilename(filetypes=[("Android APK", "*.apk")])
        if selected:
            self.apk.set(selected)

    def _refresh_async(self) -> None:
        if self.running:
            return
        self.status.set("正在读取设备…")

        def worker() -> None:
            log = Log(self._append)
            try:
                devices = list_devices(log, find_adb(log))
                self.messages.put(("devices", devices))
            except Exception as exc:
                self.messages.put(("error", f"读取设备失败：{exc}"))
            finally:
                log.close()

        threading.Thread(target=worker, daemon=True).start()

    def _start(self) -> None:
        if self.running:
            return
        serial = self.device.get().strip()
        apk = Path(self.apk.get().strip())
        if not serial or not apk.is_file():
            messagebox.showerror("无法开始", "请选择已授权设备和 APK")
            return
        self.running = True
        self.start.configure(state="disabled")
        self.refresh.configure(state="disabled")
        self.status.set("安装中，请勿拔线…")

        def worker() -> None:
            log = Log(self._append)
            try:
                result = install(log, serial, apk)
                self.messages.put(("done", (result, log.path)))
            finally:
                log.close()

        threading.Thread(target=worker, daemon=True).start()

    def _drain(self) -> None:
        try:
            while True:
                kind, payload = self.messages.get_nowait()
                if kind == "log":
                    self.log.configure(state="normal")
                    self.log.insert("end", str(payload) + "\n")
                    self.log.see("end")
                    self.log.configure(state="disabled")
                elif kind == "devices":
                    values = list(payload)
                    self.devices.configure(values=values)
                    if values:
                        self.device.set(values[0])
                        self.status.set(f"发现 {len(values)} 台设备")
                    else:
                        self.status.set("没有已授权设备")
                elif kind == "error":
                    self.status.set(str(payload))
                    messagebox.showerror("错误", str(payload))
                elif kind == "done":
                    (success, message, reboot), log_path = payload
                    self.running = False
                    self.start.configure(state="normal")
                    self.refresh.configure(state="normal")
                    suffix = f"\n日志：{log_path}"
                    self.status.set(message)
                    if success:
                        messagebox.showinfo("安装完成", message + suffix)
                    elif reboot:
                        messagebox.showwarning("需要普通重启", message + suffix)
                    else:
                        messagebox.showerror("安装失败", message + suffix)
        except queue.Empty:
            pass
        self.root.after(80, self._drain)


def main() -> int:
    if tk is None:
        raise InstallError("当前 Python 未安装 tkinter；请使用含 Tcl/Tk 的 Windows Python")
    root = tk.Tk()
    App(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
