> Tento dokument popisuje samostatné vývojové/ParaView nástroje. Pro instalaci
> Sliceru použij [INSTALLATION.md](INSTALLATION.md); nepotřebuje tento renderer.

# Windows: doplňující poznámky k backendu

**Aktuální postup včetně pluginů je v [INSTALLATION.md](INSTALLATION.md).**
Níže zůstávají podrobnosti pro ruční spouštění backendu a test na hardware.
Používejte nový build adresář, pokud přecházíte z tripletu `x64-windows`
na `x64-windows-release`; existující cache nelze mezi triplety míchat.

## 1. Nástroje

Nainstalujte Visual Studio 2022 / Build Tools 2022 s workloadem **Desktop development
with C++**, CMake, Git a Python 3.10+. Použijte x64; nemíchejte Win32, x64 a ARM64.

V PowerShellu:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:/dev/vcpkg
C:/dev/vcpkg/bootstrap-vcpkg.bat
git clone https://github.com/HonzaHovora/QAROS_VTK.git C:/dev/QAROS_VTK
cd C:/dev/QAROS_VTK
./scripts/build-windows.ps1 -VcpkgRoot C:/dev/vcpkg
./build-windows/Release/qarvtk-preview.exe
```

Manifest připíná verze závislostí. Skript zkontroluje testy včetně OpenGL;
pro GPU test je potřeba fungující ovladač OpenGL 3.3. Windows vzdálená plocha
nebo základní Microsoft OpenGL ovladač mohou místo NVIDIA poskytovat nevhodný
kontext. V náhledu se vypíše výrobce i jméno skutečně použitého rendereru.

## 2. Build s QAROS

Rozbalte **celý** SDK balík. Očekávané soubory:

```text
C:/QAROS/include/qar_streaming.h
C:/QAROS/bin/qar-streaming-c.dll
C:/QAROS/bin/qar-runtime-launcher.exe
```

Složku `bin` ponechte kompletní. SDK DLL nekopírujte mezi VTK DLL vedle aplikace.
Build aplikace používá dynamické načtení knihovny ze zvolené SDK složky.

```powershell
./scripts/build-windows.ps1 -VcpkgRoot C:/dev/vcpkg -SdkRoot C:/QAROS
./build-windows/Release/qarvtk-sender.exe --sdk-info
python scripts/run_sender.py --exe build-windows/Release/qarvtk-sender.exe --sdk C:/QAROS --preview --frames 300 --timeout 60
```

Spouštěcí skript porovná SHA-256 SDK headeru s hodnotou z binárky. Tím zachytí
záměnu headerů; není to kontrola původu ani ABI libovolně vyměněných DLL.
Header a binární soubory musí pocházet ze stejného balíku. Běžte z normálního
PowerShellu, ne z Python konzole Sliceru/ParaView s jejich upraveným prostředím.

### Nové SDK / Hub onboarding

Spusťte kompatibilní Hub. Při prvním připojení zadejte jeho aktuální párovací kód
do konzole senderu. Kód se nedává do argumentů příkazové řádky. Identita se uloží
do `qarvtk.onboarding-id.txt`; další spuštění použije rejoin. Pro odlišnou identitu
použijte `--identity C:/data/another.onboarding-id.txt`.

Ve vieweru otevřete obsah této aplikace, aby odeslal požadavek na render stream.
Prototyp obsluhuje první žádající viewer. První krychle je uprostřed objemu
umístěného 1,5 m před počátkem room space. Po ukončení se tento objem uzavře.

### SDK 0.9.0-dev.2540

Tato větev nemá nové onboarding API. Sender vytvoří vlastní session a pozve
Visualizer. Lze připojit existující session pomocí `--invite-file`, nebo zvolit
`--target quest` / `--target hololens:HOST`. Pozvánku vypisovat ani commitovat není
potřeba. Tento postup není implementací nového Hub párování se starými DLL.

## 3. První kontrola s hardwarem

1. Náhled: zelená krychle, správný NVIDIA adaptér v konzoli.
2. `ctest --test-dir build-windows -C Release --output-on-failure`: core a GPU PASS.
3. Spustit sender pro 300 snímků. Musí se objevit `Submitted 1 stereo frames`.
4. Ve vieweru zkontrolovat obě oči, orientaci, velikost 0,4 m, hloubku a pozadí.
5. Posunout hlavu: perspektiva musí reagovat bez prohození očí.
6. Otevřít vlastní testovací VTP s odpovídajícími jednotkami; znovu načíst při
   zapnutém `--watch` a ověřit aktualizaci.
7. Přerušit a obnovit spojení; ověřit reconnect v limitu `--timeout`.

Do výsledku zapsat verzi SDK/Hubu/vieweru, GPU a ovladač, rozlišení, počet odeslaných
snímků a viditelné chyby. Není zatím stanovena ani změřena cílová latence/FPS.

## Když to nejde

| Projev | Co zkontrolovat |
| --- | --- |
| Chybí `VTKConfig.cmake` | Správný `VcpkgRoot`; dokončení instalace závislostí |
| Nesouhlasí SHA headeru | Znovu CMake configure + build se správným SDK |
| DLL nelze načíst | Kompletní `bin`, x64, Visual C++ runtime a shodný balík |
| Chybí GL_EXT_memory_object | Aplikace běží na nepodporovaném adaptéru/ovladači |
| Čekání na request | Viewer musí požádat právě tuto aplikaci o stream |
| Čekání na první frame | Stav vieweru, správný peer a app volume, GPU/enkodér |
| Model není vidět | Jednotky, poloha vůči počátku, klipování 0,05–20 m |
| Model vzhůru nohama | U Sliceru použít `--ras`; ostatní zdroje jsou Y-up |

Artifacts z Windows CI neobsahují QAROS runtime. Sender z CI je sestavený proti
připnutému veřejnému headeru, takže pro starší SDK 2540 ho znovu sestavte lokálně.
