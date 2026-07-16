# Copyright (C) 2026 yoyicue
# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Work = Join-Path $env:RUNNER_TEMP 'xpad2-windows-recovery-test'
$ReleaseZip = Join-Path $Work 'release.zip'
$ReleaseDir = Join-Path $Work 'release'

Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $Work | Out-Null
New-Item -ItemType Directory -Path $ReleaseDir | Out-Null

$releaseUrl = 'https://github.com/yoyicue/xpad-installer/releases/download/v0.2.2/xpad-installer-v0.2.2-android-arm64.zip'
Invoke-WebRequest -Uri $releaseUrl -OutFile $ReleaseZip
Expand-Archive -LiteralPath $ReleaseZip -DestinationPath $ReleaseDir
$tool = Get-ChildItem -LiteralPath $ReleaseDir -Filter 'xpad-install' -File -Recurse | Select-Object -First 1
if (-not $tool) { throw 'xpad-install missing from v0.2.2 release archive' }
if ((Get-FileHash -LiteralPath $tool.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne
    '9f1ff6b7635548a11c57b2b8a31b0b98b941773bc6e0f2f00a5c3dc98e3a5fc0') {
    throw 'unexpected xpad-install hash'
}

Copy-Item -LiteralPath $tool.FullName -Destination (Join-Path $Work 'xpad-install')
$batch = Get-Content -LiteralPath (Join-Path $Root 'windows/xpad2-lockscreen-recovery.bat') -Raw
[IO.File]::WriteAllText(
    (Join-Path $Work 'START-LOCKSCREEN-RECOVERY.bat'),
    ($batch -replace "`r?`n", "`r`n"),
    [Text.UTF8Encoding]::new($false))

$fakeProject = Join-Path $Work 'fake-adb-project'
$fakeOutput = Join-Path $Work 'fake-adb-output'
dotnet new console --framework net8.0 --output $fakeProject --force | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'failed to create fake adb project' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'FakeAdb.cs') `
    -Destination (Join-Path $fakeProject 'Program.cs') -Force
dotnet publish (Get-ChildItem -LiteralPath $fakeProject -Filter '*.csproj' -File).FullName `
    --configuration Release --runtime win-x64 `
    --self-contained false -p:AssemblyName=adb -p:UseAppHost=true `
    --output $fakeOutput
if ($LASTEXITCODE -ne 0) { throw 'failed to publish fake adb project' }
Copy-Item -Path (Join-Path $fakeOutput '*') -Destination $Work -Force
if (-not (Test-Path -LiteralPath (Join-Path $Work 'adb.exe'))) {
    throw 'failed to build fake adb.exe'
}

$stdout = Join-Path $Work 'batch-stdout.txt'
$stderr = Join-Path $Work 'batch-stderr.txt'
$process = Start-Process -FilePath 'cmd.exe' -WorkingDirectory $Work `
    -ArgumentList @('/d', '/c', '(echo Y&echo X)|call START-LOCKSCREEN-RECOVERY.bat') `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue
    Get-Content -LiteralPath $stderr -ErrorAction SilentlyContinue
    throw "recovery batch exited $($process.ExitCode)"
}

$commands = Get-Content -LiteralPath (Join-Path $Work 'fake-adb-commands.log') -Raw
foreach ($required in @('self-test', 'cleanup', 'reboot', 'dumpsys lock_settings', 'dumpsys window policy')) {
    if (-not $commands.Contains($required)) { throw "missing fake adb command: $required" }
}
foreach ($forbidden in @(' doctor', 'locksettings clear', 'set-pin', 'set-pattern', 'set-password')) {
    if ($commands.Contains($forbidden)) { throw "forbidden fake adb command: $forbidden" }
}

$logZip = Get-ChildItem -LiteralPath $Work -Filter 'xpad2-lockscreen-recovery-log-*.zip' -File |
    Select-Object -First 1
if (-not $logZip) { throw 'diagnostic zip was not created' }
$logs = Join-Path $Work 'unpacked-logs'
Expand-Archive -LiteralPath $logZip.FullName -DestinationPath $logs
$summary = Get-ChildItem -LiteralPath $logs -Filter 'summary.txt' -File -Recurse | Select-Object -First 1
if (-not $summary -or -not (Select-String -LiteralPath $summary.FullName -SimpleMatch 'result=completed')) {
    throw 'diagnostic summary did not report completion'
}
$allLogs = (Get-ChildItem -LiteralPath $logs -File -Recurse |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
if ($allLogs.Contains('FAKE260')) { throw 'device serial was not redacted from diagnostic zip' }
if (-not $allLogs.Contains('<redacted-serial>')) { throw 'redaction marker missing from diagnostic zip' }

Write-Host "Windows recovery batch completed; diagnostic log: $($logZip.Name)"
