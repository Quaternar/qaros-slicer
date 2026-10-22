# Isolated updater smoke: never touches an installed Slicer or QAROS package.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('qarvtk-update-test-' + [Guid]::NewGuid().ToString('N'))
$sdk = Join-Path $fixture 'Program Files\Quaternar\QAROS'
$destination = Join-Path $fixture 'package\windows-start'
$expected = '55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c'
try {
    foreach ($dir in @('downloads', 'package\windows-start\plugins\slicer',
                       'Program Files\Quaternar\QAROS\include', 'Program Files\Quaternar\QAROS\bin')) {
        New-Item -ItemType Directory -Force -Path (Join-Path $fixture $dir) | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $repo 'ci\sdk-headers\hub-55e6e3f4\include\qar_streaming.h') -Destination (Join-Path $sdk 'include')
    foreach ($file in @('qar-streaming-c.dll', 'qar-runtime-launcher.exe', 'device-catalog.jsonc')) {
        New-Item -ItemType File -Path (Join-Path $sdk "bin\$file") | Out-Null
    }
    foreach ($file in @('qarvtk-live-dev3008.zip', 'qarvtk-live-dev3008.zip.sha256')) {
        Copy-Item -LiteralPath (Join-Path $repo "downloads\$file") -Destination (Join-Path $fixture 'downloads')
    }
    $oldPlugin = Join-Path $destination 'plugins\slicer\QarosLive.py'
    Set-Content -LiteralPath $oldPlugin -Value 'previous-plugin-sentinel'
    $retired = @(
        'package/windows-start/plugins/slicer/QarosVTK.py',
        'package/windows-start/plugins/slicer/QarosVTK.pyc',
        'package/windows-start/plugins/slicer/__pycache__/QarosVTK.cpython-310.pyc',
        'package/windows-start/plugins/slicer/qarvtk_slicer.py',
        'package/windows-start/plugins/slicer/QarosVTKLib/qarvtk_slicer.py',
        'package/windows-start/plugins/slicer/QarosVTKLib/qarvtk_scene.py',
        'package/windows-start/plugins/slicer/QarosVTKLib/qarvtk_bridge.py',
        'package/QAROS-VTK-plugins/slicer/QarosVTK.py',
        'package/QAROS-VTK-plugins/slicer/QarosVTKLib/qarvtk_slicer.py'
    )
    foreach ($name in $retired) {
        $path = Join-Path $fixture $name
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        Set-Content -LiteralPath $path -Value 'retired-module-sentinel'
    }
    $custom = Join-Path $destination 'plugins\slicer\my-settings.json'
    Set-Content -LiteralPath $custom -Value 'custom-content-sentinel'
    & (Join-Path $repo 'scripts\windows\Update-Live-Plugin.ps1') -RepoRoot $fixture -SdkRoot $sdk
    $info = Get-Content -LiteralPath (Join-Path $destination 'HOST_UPDATE_INFO.json') -Raw | ConvertFrom-Json
    foreach ($file in $info.files.PSObject.Properties) {
        if ((Get-FileHash -LiteralPath (Join-Path $destination $file.Name) -Algorithm SHA256).Hash -ine $file.Value) {
            throw "Installed file mismatch: $($file.Name)"
        }
    }
    $backup = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'package') -Directory -Filter 'live-backup-*')
    if ($backup.Count -ne 1 -or (Get-Content -LiteralPath (Join-Path $backup[0].FullName 'plugins\slicer\QarosLive.py') -Raw).Trim() -ne 'previous-plugin-sentinel') {
        throw 'Previous plugin was not backed up.'
    }
    foreach ($name in $retired) {
        if (Test-Path -LiteralPath (Join-Path $fixture $name)) { throw "Retired file survived: $name" }
        $savedName = $name.Replace('package/windows-start/', '').Replace('package/QAROS-VTK-plugins/', 'QAROS-VTK-plugins/')
        if ((Get-Content -LiteralPath (Join-Path $backup[0].FullName $savedName) -Raw).Trim() -ne 'retired-module-sentinel') {
            throw "Retired file was not backed up: $name"
        }
    }
    if ((Get-Content -LiteralPath $custom -Raw).Trim() -ne 'custom-content-sentinel') { throw 'Custom content was modified.' }

    # Fail after replacement and partial retirement; all prior files must return.
    Set-Content -LiteralPath $oldPlugin -Value 'rollback-live-sentinel'
    foreach ($name in $retired) { Set-Content -LiteralPath (Join-Path $fixture $name) -Value 'rollback-retired-sentinel' }
    $global:QarUpdateTestFailurePath = Join-Path $destination 'plugins\slicer\qarvtk_slicer.py'
    $global:QarUpdateTestFailureTriggered = $false
    function Remove-Item {
        [CmdletBinding()]
        param([string]$LiteralPath, [switch]$Force, [switch]$Recurse)
        if ($LiteralPath -eq $global:QarUpdateTestFailurePath -and -not $global:QarUpdateTestFailureTriggered) {
            $global:QarUpdateTestFailureTriggered = $true
            throw 'Injected retirement failure'
        }
        Microsoft.PowerShell.Management\Remove-Item @PSBoundParameters
    }
    $failed = $false
    try { & (Join-Path $repo 'scripts\windows\Update-Live-Plugin.ps1') -RepoRoot $fixture -SdkRoot $sdk }
    catch { $failed = $_.Exception.Message -like '*Injected retirement failure*' }
    finally { Microsoft.PowerShell.Management\Remove-Item Function:\Remove-Item }
    if (-not $failed -or -not $global:QarUpdateTestFailureTriggered) { throw 'Rollback fixture did not reach retirement.' }
    if ((Get-Content -LiteralPath $oldPlugin -Raw).Trim() -ne 'rollback-live-sentinel') { throw 'Live plugin rollback failed.' }
    foreach ($name in $retired) {
        if ((Get-Content -LiteralPath (Join-Path $fixture $name) -Raw).Trim() -ne 'rollback-retired-sentinel') {
            throw "Retired module rollback failed: $name"
        }
    }
    & (Join-Path $repo 'scripts\windows\Update-Live-Plugin.ps1') -RepoRoot $fixture -SdkRoot $sdk

    # A new checkout must install directly, without an old preview/starter bundle.
    $fresh = Join-Path $fixture 'fresh-checkout'
    New-Item -ItemType Directory -Force -Path $fresh | Out-Null
    & (Join-Path $repo 'scripts\windows\Update-Live-Plugin.ps1') -RepoRoot $fresh -SdkRoot $sdk -ArchivePath (Join-Path $fixture 'downloads\qarvtk-live-dev3008.zip')
    $freshSlicer = Join-Path $fresh 'package\windows-start\plugins\slicer'
    $modules = @(Get-ChildItem -LiteralPath $freshSlicer -File -Filter '*.py')
    if ($modules.Count -ne 1 -or $modules[0].Name -ne 'QarosLive.py') { throw 'Fresh install did not contain exactly the Live module.' }
    if (-not (Test-Path -LiteralPath (Join-Path $fresh 'package\windows-start\Release\qarvtk-host.dll'))) { throw 'Fresh install DLL missing.' }
    if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
        python (Join-Path $repo 'tests\test_host_abi.py') (Join-Path $destination 'Release\qarvtk-host.dll') $expected
        if ($LASTEXITCODE -ne 0) { throw 'Shipped DLL ABI test failed.' }
    } else {
        Write-Host 'SKIP: native Windows DLL loading (requires Windows).'
    }
    $installedHash = (Get-FileHash -LiteralPath $oldPlugin -Algorithm SHA256).Hash
    Set-Content -LiteralPath (Join-Path $sdk 'include\qar_streaming.h') -Value 'wrong SDK header'
    $rejected = $false
    try { & (Join-Path $repo 'scripts\windows\Update-Live-Plugin.ps1') -RepoRoot $fixture -SdkRoot $sdk }
    catch { $rejected = $_.Exception.Message -like '*requires the installed dev.3008*' }
    if (-not $rejected -or (Get-FileHash -LiteralPath $oldPlugin -Algorithm SHA256).Hash -ne $installedHash) {
        throw 'Incompatible SDK was not rejected before modifying the plugin.'
    }
    Write-Host 'PASS: fresh Live-only install, legacy removal/cache cleanup, backup, rollback and SDK mismatch rejection.'
} finally {
    Remove-Variable QarUpdateTestFailurePath, QarUpdateTestFailureTriggered -Scope Global -ErrorAction SilentlyContinue
    # ctypes loaded the shipped DLL in a child Python process, which has exited.
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
