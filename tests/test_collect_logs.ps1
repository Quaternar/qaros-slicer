# Isolated collector check, including QAROS's separate GUI process log.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('qarvtk-collector-test-' + [Guid]::NewGuid().ToString('N'))
$previousLocalAppData = $env:LOCALAPPDATA
try {
    foreach ($dir in @('scripts\windows', 'package\windows-start\Release',
                      'sdk\include', 'sdk\bin', 'local', 'qaros\qar-launcher-default\session')) {
        New-Item -ItemType Directory -Force -Path (Join-Path $fixture $dir) | Out-Null
    }
    $env:LOCALAPPDATA = Join-Path $fixture 'local'
    $script = Join-Path $fixture 'scripts\windows\Collect-Logs.ps1'
    Copy-Item -LiteralPath (Join-Path $repo 'scripts\windows\Collect-Logs.ps1') -Destination $script
    $dll = Join-Path $fixture 'package\windows-start\Release\qarvtk-host.dll'
    Set-Content -LiteralPath $dll -Value 'fixture-native-payload'
    $hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    @{ build='collector-fixture'; files=@{'Release/qarvtk-host.dll'=$hash} } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'package\windows-start\HOST_UPDATE_INFO.json')
    $session = Join-Path $fixture 'qaros\qar-launcher-default\session'
    Set-Content -LiteralPath (Join-Path $session 'qar-stereokit-gui.log') -Value 'fixture-panel-initialization'
    Set-Content -LiteralPath (Join-Path $session 'crash-report.txt') -Value 'fixture-WebCore-crash'
    Set-Content -LiteralPath (Join-Path $session 'crash.dmp') -Value 'must-not-copy-dump'
    Set-Content -LiteralPath (Join-Path $session 'identity.json') -Value 'must-not-copy-state'
    $out = Join-Path $fixture 'output'
    & $script -SdkRoot (Join-Path $fixture 'sdk') -OutputRoot $out -QarosLogRoot (Join-Path $fixture 'qaros') -NoOpen
    $result = @(Get-ChildItem -LiteralPath $out -Directory)
    if ($result.Count -ne 1) { throw 'Expected one diagnostic output folder.' }
    $files = @(Get-ChildItem -LiteralPath $result[0].FullName -File)
    if (-not ($files | Where-Object Name -like '*qar-stereokit-gui.log')) { throw 'Missing GUI process log.' }
    if (-not ($files | Where-Object Name -like '*crash-report.txt')) { throw 'Missing crash report.' }
    if ($files | Where-Object Name -match '\.dmp$|identity\.json$') { throw 'Collector copied dump or session state.' }
    if (-not ($files | Where-Object Name -eq 'HOST_UPDATE_INFO.json')) { throw 'Valid hash-map manifest was omitted.' }
    $notes = Get-Content -LiteralPath (Join-Path $result[0].FullName 'collection-info.txt') -Raw
    if ($notes -notmatch 'DUMP \(not copied\)') { throw 'Missing dump location note.' }
    if (@(Get-ChildItem -LiteralPath $out -File -Filter '*.zip').Count -ne 1) { throw 'Missing diagnostics ZIP.' }
    Write-Host 'PASS: GUI/crash logs, valid update manifest, dump/state exclusion and ZIP.'
} finally {
    $env:LOCALAPPDATA = $previousLocalAppData
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
