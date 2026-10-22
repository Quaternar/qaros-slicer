# Lokální vývoj Slicer bridge na Windows

Ověřeno 22. 10. 2026 ve zdrojovém commitu
`d9d523e6aeb4c95ae237c8bb8fe913ce4b8186a9` na Windows x64.
Projekt byl sestaven v `D:\QAROS_VTK\build-host`, dočasné soubory v
`D:\QAROS_VTK\build-host\tmp`. Výsledkem je
`build-host\Release\qarvtk-host.dll`.

## Nástroje a SDK

- Visual Studio Community 2022 17.9, C++ workload, MSVC 19.39.33523.
- CMake 3.28 dodaný s Visual Studio; nemusí být v PATH.
- Python 3.13.6 pro testy.
- Nainstalovaný kompletní QAROS v `C:\Program Files\Quaternar\QAROS`.
- SDK hlavička SHA-256
  `55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c`.

Pro tento host bridge není potřeba stahovat ani sestavovat Slicer, VTK, Qt
nebo vcpkg. Slicer poskytuje vlastní renderer; sestavuje se jeho QAROS transport.

## Opakovatelné sestavení na disku D:

Následující příkazy spusť v PowerShellu z checkoutu na disku D:.
Existující instalace nástrojů a QAROS mohou zůstat na C:.

```powershell
Set-Location D:\QAROS_VTK

$qarVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$qarVs = & $qarVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$qarCmakeBin = Join-Path $qarVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$qarCmake = Join-Path $qarCmakeBin 'cmake.exe'
$qarCtest = Join-Path $qarCmakeBin 'ctest.exe'
$qarBuild = Join-Path (Get-Location).Path 'build-host'
$qarTemp = Join-Path $qarBuild 'tmp'
$qarOldTemp = $env:TEMP
$qarOldTmp = $env:TMP
New-Item -ItemType Directory -Path $qarTemp -Force | Out-Null

try {
    $env:TEMP = $qarTemp
    $env:TMP = $qarTemp
    & $qarCmake -S . -B $qarBuild -G 'Visual Studio 17 2022' -A x64 `
        -DQARVTK_BUILD_RENDERER=OFF -DQARVTK_BUILD_HOST_BRIDGE=ON `
        '-DQAROS_SDK_ROOT=C:/Program Files/Quaternar/QAROS'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $qarCmake --build $qarBuild --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & $qarCtest --test-dir $qarBuild -C Release --output-on-failure --timeout 60
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
} finally {
    $env:TEMP = $qarOldTemp
    $env:TMP = $qarOldTmp
}
```

Pokud CMake nenajde skutečný Python, přidej při configure
`-DPython3_EXECUTABLE=...` s cestou k instalovanému interpretu.

## Výsledek místního ověření

Release build prošel. CTest: **15/15 PASS**, 15,73 s.
Prošly core, frame lease, plánování a startup streamů, konfigurace, Hub,
přítomnost peerů, konfigurace panelů, legacy peer/frame wait, WGL callbacks,
Python host runtime, diagnostika, ABI sestavené DLL a řízení testovacího procesu.

Tento běh neověřoval přenos ze Sliceru do brýlí, GUI titulky, opakovaný
Stop/Start se skutečnými přijímači ani výkon streamingu. Otevřené chyby jsou
v [HANDOVER.md](../HANDOVER.md). Instalační ZIP zůstává původní r8.4;
nová lokální DLL nebyla instalována do běžícího Sliceru ani přibalena do ZIPu.

Aktualizátor a historické odkazy na balíčky při importu nadále směřují na
zdrojový `HonzaHovora/QAROS_VTK`. Změna distribuce balíčků je samostatný krok.
