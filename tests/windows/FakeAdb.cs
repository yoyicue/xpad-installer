// Copyright (C) 2026 yoyicue
// SPDX-License-Identifier: GPL-3.0-only

using System;
using System.IO;

public static class FakeAdb
{
    private const string Serial = "FAKE260";
    private const string Fingerprint =
        "alps/vnd_ls12_mt8797_wifi_64/ls12_mt8797_wifi_64:13/TP1A.220624.014/260:user/release-keys";
    private const string ToolHash =
        "9f1ff6b7635548a11c57b2b8a31b0b98b941773bc6e0f2f00a5c3dc98e3a5fc0";

    public static int Main(string[] args)
    {
        string root = AppDomain.CurrentDomain.BaseDirectory;
        string command = string.Join(" ", args);
        File.AppendAllText(Path.Combine(root, "fake-adb-commands.log"), command + Environment.NewLine);
        string rebooted = Path.Combine(root, "fake-adb-rebooted");

        if (command == "version")
        {
            Console.WriteLine("Android Debug Bridge version 1.0.41");
            return 0;
        }
        if (command == "start-server") return 0;
        if (command == "devices -l")
        {
            Console.WriteLine("List of devices attached");
            Console.WriteLine(Serial + " device product:vnd_ls12_mt8797_wifi_64 model:TALIH_PD2");
            return 0;
        }
        if (command.Contains(" reboot"))
        {
            File.WriteAllText(rebooted, "1");
            return 0;
        }
        if (command.Contains(" get-state")) return File.Exists(rebooted) ? 1 : 0;
        if (command.Contains(" push ") || command.Contains(" pull ")) return 0;
        if (!command.Contains(" shell ")) return 0;

        if (command.Contains("getprop ro.build.fingerprint"))
            Console.WriteLine(Fingerprint);
        else if (command.Contains("getprop sys.boot_completed"))
            Console.WriteLine("1");
        else if (command.Contains("xpad-install-recovery-v0.2.2") && command.Contains("sha256sum"))
            Console.WriteLine(ToolHash + "  /data/local/tmp/xpad-install-recovery-v0.2.2");
        else if (command.Contains(" self-test"))
            Console.WriteLine("XPAD_INSTALL_SELF_TEST status=ok version=0.2.2 dex_size=100 anchor_size=100");
        else if (command.Contains(" cleanup"))
            Console.WriteLine("cleanup=complete");
        else if (command.Contains("locksettings get-disabled"))
            Console.WriteLine("false");
        else if (command.Contains("dumpsys lock_settings"))
            Console.WriteLine("User 0\n    Quality: 0\n    CredentialType: None");
        else if (command.Contains("dumpsys window policy"))
            Console.WriteLine("KeyguardServiceDelegate showing=true serial=" + Serial);
        else if (command.Contains("logcat "))
            Console.WriteLine("SystemUI Keyguard stable device=" + Serial);
        else if (command.Contains("dumpsys dropbox"))
            Console.WriteLine("No entries found.");
        else if (command.Contains("cat /proc/sys/kernel/random/boot_id"))
            Console.WriteLine(File.Exists(rebooted)
                ? "22222222-2222-2222-2222-222222222222"
                : "11111111-1111-1111-1111-111111111111");
        else if (command.Contains("getenforce"))
            Console.WriteLine("Enforcing");
        else if (command.Contains("pidof zygote64"))
            Console.WriteLine(File.Exists(rebooted) ? "1593" : "593");
        else if (command.Contains("pidof zygote"))
            Console.WriteLine(File.Exists(rebooted) ? "1594" : "594");
        else if (command.Contains("pidof system_server"))
            Console.WriteLine(File.Exists(rebooted) ? "2056" : "1056");
        else if (command.Contains("pidof com.android.systemui"))
            Console.WriteLine(File.Exists(rebooted) ? "2366" : "1366");
        else if (command.Contains("hidden_api_blacklist_exemptions") && command.Contains("wc -c"))
            Console.WriteLine("5");
        else if (command.Contains("hidden_api_blacklist_exemptions") && command.Contains("sha256sum"))
            Console.WriteLine("0000000000000000000000000000000000000000000000000000000000000000  -");
        else if (command.Contains("hidden_api_blacklist_exemptions") && command.Contains("grep -q"))
            Console.WriteLine("1");
        else if (command.Contains("am start"))
            Console.WriteLine("Starting: Intent");

        return 0;
    }
}
