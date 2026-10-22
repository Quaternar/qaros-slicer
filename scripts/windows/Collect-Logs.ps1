param(
    [string]$SdkRoot = '',
    [string]$OutputRoot = '',
    [string]$QarosLogRoot = '',
    [switch]$NoOpen
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $SdkRoot) {
    try {
        $registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
        try {
            $key = $registry.OpenSubKey('SOFTWARE\Quaternar\QAROS')
            if ($key) { try { $SdkRoot = [string]$key.GetValue('InstallFolder') } finally { $key.Dispose() } }
        } finally { $registry.Dispose() }
    } catch { }
    if (-not $SdkRoot) {
        $programFilesRoot = if ($env:ProgramFiles) { $env:ProgramFiles } else { 'C:\Program Files' }
        $SdkRoot = Join-Path $programFilesRoot 'Quaternar\QAROS'
    }
}
if (-not $OutputRoot) { $OutputRoot = Join-Path $repoRoot 'diagnostics' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$folder = Join-Path $OutputRoot ("QAROS-VTK-diagnostics-" + $stamp)
$archive = $folder + '.zip'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$notes = New-Object 'System.Collections.Generic.List[string]'
$notes.Add('Collected at: ' + (Get-Date -Format o))
$notes.Add('PowerShell: ' + $PSVersionTable.PSVersion.ToString())
$notes.Add('This collector does not launch, stop or modify the sender or viewer.')
$notes.Add('Sender/live-host/SDK and recent QAROS text logs, build metadata and related crash events are collected. Dumps are listed, not copied.')
$notes.Add('QAROS installation: ' + $SdkRoot)

# Each Slicer/ParaView plugin instance has a separate qarvtk-* directory.
# Copy only sender.log, never scene.vtp, invites or onboarding identities.
$logs = @(Get-ChildItem -LiteralPath ([IO.Path]::GetTempPath()) -Directory -Filter 'qarvtk-*' |
    ForEach-Object {
        $path = Join-Path $_.FullName 'sender.log'
        if (Test-Path -LiteralPath $path -PathType Leaf) { Get-Item -LiteralPath $path }
    } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 3)
$index = 0
foreach ($log in $logs) {
    $index++
    $target = Join-Path $folder ("sender-" + $index + '.log')
    $notes.Add(('sender-{0}.log: {1}; modified {2:o}; {3} bytes' -f $index, $log.FullName, $log.LastWriteTime, $log.Length))
    try {
        if ($log.Length -le 25MB) {
            Copy-Item -LiteralPath $log.FullName -Destination $target
        } else {
            $notes.Add("sender-$index.log was over 25 MiB: included first 100 and last 10000 lines.")
            Get-Content -LiteralPath $log.FullName -Encoding UTF8 -TotalCount 100 | Set-Content -LiteralPath $target -Encoding UTF8
            '--- LARGE LOG: middle omitted by collector ---' | Add-Content -LiteralPath $target -Encoding UTF8
            Get-Content -LiteralPath $log.FullName -Encoding UTF8 -Tail 10000 | Add-Content -LiteralPath $target -Encoding UTF8
        }
    } catch { $notes.Add("Cannot read sender log: $_") }
}
if ($logs.Count -eq 0) { $notes.Add('No sender.log found in qarvtk-* temporary folders.') }

$liveRoot = Join-Path $env:LOCALAPPDATA 'QAROS_VTK\live-logs'
if (Test-Path -LiteralPath $liveRoot -PathType Container) {
    $liveLogs = @(Get-ChildItem -LiteralPath $liveRoot -Directory |
        ForEach-Object {
            $path = Join-Path $_.FullName 'host-bridge.log'
            if (Test-Path -LiteralPath $path -PathType Leaf) { Get-Item -LiteralPath $path }
        } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 3)
    $liveIndex = 0
    foreach ($log in $liveLogs) {
        $liveIndex++
        $target = Join-Path $folder ("host-bridge-" + $liveIndex + '.log')
        $notes.Add(('host-bridge-{0}.log: {1}; modified {2:o}; {3} bytes' -f $liveIndex, $log.FullName, $log.LastWriteTime, $log.Length))
        try {
            Get-Content -LiteralPath $log.FullName -Encoding UTF8 -Tail 10000 |
                Set-Content -LiteralPath $target -Encoding UTF8
        } catch { $notes.Add("Cannot read host bridge log: $_") }
        $pythonLog = Join-Path $log.DirectoryName 'host-python.log'
        if (Test-Path -LiteralPath $pythonLog -PathType Leaf) {
            try {
                Get-Content -LiteralPath $pythonLog -Encoding UTF8 -Tail 10000 |
                    Set-Content -LiteralPath (Join-Path $folder ("host-python-" + $liveIndex + '.log')) -Encoding UTF8
            } catch { $notes.Add("Cannot read live Python log: $_") }
        }
        $systemLog = Join-Path $log.DirectoryName 'host-system.log'
        if (Test-Path -LiteralPath $systemLog -PathType Leaf) {
            try {
                Get-Content -LiteralPath $systemLog -Encoding UTF8 -Tail 10000 |
                    Set-Content -LiteralPath (Join-Path $folder ("host-system-" + $liveIndex + '.log')) -Encoding UTF8
            } catch { $notes.Add("Cannot read live system log: $_") }
        }
        # qar_library_init writes its detailed SDK logs into this exact run's
        # log_folder_path. Previously the collector omitted them, losing the
        # receiver/stream-negotiation evidence behind QueryMatchingStream.
        $sdkLogs = @(Get-ChildItem -LiteralPath $log.DirectoryName -File -Recurse |
            Where-Object {
                $_.Name -match '\.log(?:\.\d+)?$' -and
                $_.Name -notmatch '^host-(bridge|python|system)\.log$'
            } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 12)
        $sdkIndex = 0
        foreach ($sdkLog in $sdkLogs) {
            $sdkIndex++
            $sdkTarget = 'host-sdk-{0}-{1}.log' -f $liveIndex, $sdkIndex
            $notes.Add(('{0}: {1}; modified {2:o}; {3} bytes' -f $sdkTarget, $sdkLog.FullName, $sdkLog.LastWriteTime, $sdkLog.Length))
            try {
                Get-Content -LiteralPath $sdkLog.FullName -Encoding UTF8 -Tail 10000 |
                    Set-Content -LiteralPath (Join-Path $folder $sdkTarget) -Encoding UTF8
            } catch { $notes.Add("Cannot read live SDK log: $_") }
        }
        if ($sdkLogs.Count -eq 0) {
            $notes.Add("No additional .log files in live run $liveIndex. File names:")
            Get-ChildItem -LiteralPath $log.DirectoryName -File -Recurse |
                Select-Object -First 30 | ForEach-Object { $notes.Add($_.FullName) }
        }
    }
}

# The access violation is in qar-stereokit-gui, a separate process. The plugin's
# SDK log cannot show that process's panel initialization or WebCore messages.
# Stay within QAROS's log tree; never copy session state, pairing data or dumps.
if (-not $QarosLogRoot) { $QarosLogRoot = Join-Path ([IO.Path]::GetTempPath()) 'quaternar\logs' }
if (Test-Path -LiteralPath $qarosLogRoot -PathType Container) {
    $recentQaros = @(Get-ChildItem -LiteralPath $qarosLogRoot -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge (Get-Date).AddHours(-2) })
    $qarosLogs = @($recentQaros | Where-Object { $_.Name -match '(?i)\.log(?:\.\d+)?$|\.txt$' } |
        Sort-Object @{Expression={ if ($_.FullName -match '(?i)stereokit[-_]gui|crash') { 0 } else { 1 } }},
                    @{Expression='LastWriteTimeUtc'; Descending=$true} | Select-Object -First 32)
    $qarosIndex = 0
    foreach ($log in $qarosLogs) {
        $qarosIndex++
        $targetName = 'qaros-{0}-{1}' -f $qarosIndex, $log.Name
        $notes.Add(('{0}: {1}; modified {2:o}; {3} bytes' -f $targetName, $log.FullName, $log.LastWriteTime, $log.Length))
        try {
            Get-Content -LiteralPath $log.FullName -Encoding UTF8 -Tail 10000 |
                Set-Content -LiteralPath (Join-Path $folder $targetName) -Encoding UTF8
        } catch { $notes.Add("Cannot read QAROS process log: $_") }
    }
    $recentQaros | Where-Object { $_.Extension -eq '.dmp' } |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 10 |
        ForEach-Object { $notes.Add(('DUMP (not copied): {0}; modified {1:o}; {2} bytes' -f $_.FullName, $_.LastWriteTime, $_.Length)) }
    if (-not $qarosLogs.Count) { $notes.Add('No recent QAROS text logs found under ' + $qarosLogRoot) }
} else { $notes.Add('QAROS log folder not found: ' + $qarosLogRoot) }

$install = Join-Path $repoRoot 'package\windows-start'
foreach ($name in @('BUILD_INFO.json', 'HOST_UPDATE_INFO.json', 'qarvtk-sdk.json')) {
    $path = Join-Path $install $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if ($name -eq 'HOST_UPDATE_INFO.json') {
            try {
                $update = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
                $installedHash = (Get-FileHash -LiteralPath (Join-Path $install 'Release\qarvtk-host.dll') -Algorithm SHA256).Hash
                $expectedHash = if ($update.files) { $update.files.'Release/qarvtk-host.dll' } else { $update.dll_sha256 }
                if (-not $expectedHash -or $installedHash -ne $expectedHash) {
                    $notes.Add('HOST_UPDATE_INFO.json is stale (DLL hash differs, for example after restoring r17); omitted.')
                    continue
                }
            } catch { $notes.Add("Cannot validate host update metadata: $_"); continue }
        }
        Copy-Item -LiteralPath $path -Destination (Join-Path $folder $name)
    }
}
foreach ($path in @((Join-Path $install 'Release\qarvtk-sender.exe'), (Join-Path $install 'Release\qarvtk-host.dll'), (Join-Path $SdkRoot 'include\qar_streaming.h'), (Join-Path $SdkRoot 'bin\qar-streaming-c.dll'), (Join-Path $SdkRoot 'bin\qar-stereokit-gui.exe'), (Join-Path $SdkRoot 'bin\WebCore.dll'), (Join-Path $SdkRoot 'bin\Ultralight.dll'))) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        try {
            $hash = Get-FileHash -LiteralPath $path -Algorithm SHA256
            $notes.Add('SHA256 ' + $hash.Hash + '  ' + $path)
            if ($path -match '\.(exe|dll)$') {
                $version = (Get-Item -LiteralPath $path).VersionInfo
                $notes.Add(('VERSION {0}; product {1}; {2}' -f $version.FileVersion, $version.ProductVersion, $path))
            }
        } catch { $notes.Add("Cannot hash file: $_") }
    } else { $notes.Add('Missing file: ' + $path) }
}

# Windows Application Error / WER / .NET crash records can identify the failing
# executable and faulting module even if its own log ends without an error.
$processPattern = '(?i)(qar[^\s<>]*\.exe|visuali[sz]er[^\s<>]*\.exe|slicer(?:app-real)?\.exe)'
try {
    $events = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'; Id = @(1000, 1001, 1026); StartTime = (Get-Date).AddHours(-2)
    } -MaxEvents 500 -ErrorAction Stop | Where-Object {
        $_.ToXml() -match $processPattern
    } | Select-Object -First 20)
    if ($events.Count -gt 0) {
        $events | Select-Object TimeCreated, Id, ProviderName, Message |
            Format-List | Out-String -Width 4096 |
            Set-Content -LiteralPath (Join-Path $folder 'windows-crashes.txt') -Encoding UTF8
        $events | ForEach-Object { $_.ToXml() } |
            Set-Content -LiteralPath (Join-Path $folder 'windows-crashes-raw.txt') -Encoding UTF8
        $notes.Add('Related Windows crash records: ' + $events.Count)
    } else { $notes.Add('No related Windows crash records found in the last two hours.') }
} catch { $notes.Add("Windows crash records unavailable or no events found: $_") }

try {
    $processes = @(Get-Process | Where-Object {
        $_.ProcessName -match '(?i)(^qar|visuali[sz]er|^slicer)'
    } | Select-Object ProcessName, Id, Path)
    $processes | Format-Table -AutoSize | Out-String -Width 4096 |
        Set-Content -LiteralPath (Join-Path $folder 'running-processes.txt') -Encoding UTF8
} catch { $notes.Add("Cannot list related running processes: $_") }

$notes | Set-Content -LiteralPath (Join-Path $folder 'collection-info.txt') -Encoding UTF8
Compress-Archive -Path (Join-Path $folder '*') -DestinationPath $archive
Write-Host ''
Write-Host ('Created: ' + $archive)
Write-Host ('Sender logs found: ' + $logs.Count)
if (-not $NoOpen) {
    Start-Process explorer.exe -ArgumentList ('/select,"' + $archive + '"')
}
