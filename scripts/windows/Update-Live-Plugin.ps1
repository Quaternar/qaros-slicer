param(
    [string]$SdkRoot = '',
    [string]$RepoRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$ArchivePath = ''
)
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$expectedSdk = '55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c'
if (-not $SdkRoot) {
    $machine = $null
    $key = $null
    try {
        $machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
        $key = $machine.OpenSubKey('SOFTWARE\Quaternar\QAROS')
        if ($key) { $SdkRoot = [string]$key.GetValue('InstallFolder', '') }
    } catch {
        # No installation registration: the standard folder remains usable.
    } finally {
        if ($key) { $key.Dispose() }
        if ($machine) { $machine.Dispose() }
    }
    if (-not $SdkRoot) {
        $programs = if ($env:ProgramW6432) { $env:ProgramW6432 } elseif ($env:ProgramFiles) { $env:ProgramFiles } else { 'C:\Program Files' }
        $SdkRoot = Join-Path $programs 'Quaternar\QAROS'
    }
}
$destination = Join-Path $RepoRoot 'package\windows-start'
$archive = if ($ArchivePath) { $ArchivePath } else { Join-Path $RepoRoot 'downloads\qarvtk-live-dev3008.zip' }

if (Get-Process -Name 'Slicer', 'SlicerApp-real' -ErrorAction SilentlyContinue) {
    throw 'Close Slicer before updating its plugin.'
}
foreach ($name in @('include\qar_streaming.h', 'bin\qar-streaming-c.dll', 'bin\qar-runtime-launcher.exe', 'bin\device-catalog.jsonc')) {
    if (-not (Test-Path -LiteralPath (Join-Path $SdkRoot $name) -PathType Leaf)) {
        throw "Incomplete QAROS package: $SdkRoot\$name is missing. Select the SDK root containing include and bin."
    }
}
if ((Get-FileHash -LiteralPath (Join-Path $SdkRoot 'include\qar_streaming.h') -Algorithm SHA256).Hash -ine $expectedSdk) {
    throw 'This update requires the installed dev.3008 SDK header 55e6e3f4. Do not replace individual SDK files.'
}
$expected = (Get-Content -LiteralPath ($archive + '.sha256') -Raw).Trim().Split(' ')[0]
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $expected) {
    throw 'Live plugin archive checksum mismatch. Pull the complete update from GitHub.'
}

$stamp = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$staging = Join-Path $RepoRoot "package\live-update-$stamp"
$backup = Join-Path $RepoRoot "package\live-backup-$stamp"
$files = @(
    'Release/qarvtk-host.dll',
    'plugins/slicer/QarosLive.py',
    'plugins/slicer/QarosVTKLib/__init__.py',
    'plugins/slicer/QarosVTKLib/qarvtk_host_runtime.py',
    'plugins/slicer/QarosVTKLib/qarvtk_host_view.py',
    'plugins/slicer/QarosVTKLib/qarvtk_host_gpu.py',
    'plugins/slicer/QarosVTKLib/qarvtk_host_panels.py',
    'plugins/slicer/QarosVTKLib/qarvtk_host_diagnostics.py',
    'plugins/slicer/QarosVTKLib/qarvtk_runtime.py',
    'HOST_UPDATE_INFO.json'
)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    if ($names.Count -ne $files.Count -or @($names | Select-Object -Unique).Count -ne $files.Count) {
        throw 'Unexpected or duplicate files in update archive.'
    }
    foreach ($name in $names) {
        if ($name -cnotin $files) { throw "Unexpected update path: $name" }
    }
} finally { $zip.Dispose() }

Expand-Archive -LiteralPath $archive -DestinationPath $staging
$info = Get-Content -LiteralPath (Join-Path $staging 'HOST_UPDATE_INFO.json') -Raw | ConvertFrom-Json
if ($info.sdk_header_sha256 -ne $expectedSdk -or $info.host_abi -ne 5) {
    throw 'Archive SDK or ABI metadata differs from this updater.'
}
if ($info.multi_target -ne 1 -or $info.build -ne 'installed-hub-update-r8.4' -or $info.native_build -ne 'installed-hub-update-r8.4' -or $info.runtime_teardown -ne 'handle_only' -or $info.target_discovery -ne 3 -or $info.panel_probe -ne 0 -or $info.slice_panel_diagonal_inches -ne 14 -or $info.panel_size_on_start -ne 1 -or $info.panel_image_region -ne 1 -or $info.orientation_mode -ne 'fixed_ras') {
    throw 'This updater requires the r8.4 package with peer presence discovery and image-only 14-inch panels.'
}
foreach ($relative in $files) {
    if ($relative -eq 'HOST_UPDATE_INFO.json') { continue }
    $hash = (Get-FileHash -LiteralPath (Join-Path $staging $relative) -Algorithm SHA256).Hash
    if ($hash -ine $info.files.$relative) { throw "Payload checksum mismatch: $relative" }
}

# Remove only known files of the retired Slicer exporter, including import caches.
# The second path covers users who previously packaged the Python modules locally.
$retired = @()
$roots = @(
    @{ Path = (Join-Path $destination 'plugins\slicer'); Backup = 'plugins/slicer' },
    @{ Path = (Join-Path $RepoRoot 'package\QAROS-VTK-plugins\slicer'); Backup = 'QAROS-VTK-plugins/slicer' }
)
foreach ($root in $roots) {
    foreach ($subdir in @('', 'QarosVTKLib')) {
        $directory = if ($subdir) { Join-Path $root.Path $subdir } else { $root.Path }
        $savedDirectory = if ($subdir) { Join-Path $root.Backup $subdir } else { $root.Backup }
        $names = if ($subdir) { @('qarvtk_slicer', 'qarvtk_bridge', 'qarvtk_scene') } else { @('QarosVTK', 'qarvtk_slicer', 'qarvtk_bridge', 'qarvtk_scene', 'qarvtk_runtime') }
        foreach ($name in $names) {
            foreach ($suffix in @('.py', '.pyc', '.pyo')) {
                $source = Join-Path $directory ($name + $suffix)
                if (Test-Path -LiteralPath $source -PathType Leaf) {
                    $retired += @{ Source = $source; Saved = (Join-Path $backup (Join-Path $savedDirectory ($name + $suffix))) }
                }
            }
            $cache = Join-Path $directory '__pycache__'
            if (Test-Path -LiteralPath $cache -PathType Container) {
                foreach ($item in @(Get-ChildItem -LiteralPath $cache -File -Filter ($name + '.*.pyc'))) {
                    $retired += @{ Source = $item.FullName; Saved = (Join-Path $backup (Join-Path $savedDirectory ('__pycache__/' + $item.Name))) }
                }
            }
        }
    }
}

# Back up every replacement and retirement before changing the installation.
New-Item -ItemType Directory -Force -Path $backup | Out-Null
foreach ($item in $retired) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $item.Saved) | Out-Null
    Copy-Item -LiteralPath $item.Source -Destination $item.Saved
}
$existing = @{}
foreach ($relative in $files) {
    $target = Join-Path $destination $relative
    $existing[$relative] = Test-Path -LiteralPath $target -PathType Leaf
    if ($existing[$relative]) {
        $saved = Join-Path $backup $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $saved) | Out-Null
        Copy-Item -LiteralPath $target -Destination $saved
    }
}
try {
    foreach ($relative in $files) {
        $target = Join-Path $destination $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath (Join-Path $staging $relative) -Destination $target -Force
    }
    foreach ($item in $retired) {
        Remove-Item -LiteralPath $item.Source -Force
    }
} catch {
    $failure = $_
    foreach ($relative in $files) {
        $target = Join-Path $destination $relative
        if ($existing[$relative]) {
            Copy-Item -LiteralPath (Join-Path $backup $relative) -Destination $target -Force
        } elseif (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target
        }
    }
    foreach ($item in $retired) {
        Copy-Item -LiteralPath $item.Saved -Destination $item.Source -Force
    }
    throw $failure
}
Remove-Item -LiteralPath $staging -Recurse
Write-Host 'QAROS Live Renderer r8.4 installed for QAROS 0.9.0-dev.3008+65dc9aeb (ABI 5).'
Write-Host 'The retired QAROS Streaming module and its export helpers were removed from managed plugin folders.'
Write-Host "Previous plugin saved to: $backup"
Write-Host "Slicer Additional module paths: $(Join-Path $destination 'plugins\slicer')"
Write-Host "Open Slicer, QAROS Live Renderer. SDK folder: $SdkRoot"
Write-Host 'In QAROS, start one Visualizer receiver and/or connect HoloLens. Either can join later.'
Write-Host 'Stream to: Visualizer + headset is the default. Headset only and Visualizer only are also available.'
Write-Host 'Start live streaming in Slicer; allow Slicer in the QAROS Hub approval window if prompted.'
Write-Host 'Slice panels start automatically. The step-by-step test mode has been removed.'
Write-Host 'Each slice panel starts at a 14-inch diagonal, preserving aspect ratio. Resize it in QAROS as needed.'
Write-Host 'Model size defaults to 1:1. Use the 1/8x to 8x slider before Start; centering preserves size.'
Write-Host 'Stop now releases the Slicer runtime handle without requesting shared QAROS runtime shutdown.'
Write-Host 'Keep QAROS and headset players on the same version; approve or pair again if QAROS requests it.'
