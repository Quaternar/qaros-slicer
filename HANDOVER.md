# QAROS Slicer — předání vývoje Lukášovi

Stav k 22. 10. 2026. Jan předává projekt k dalším opravám do
`Quaternar/qaros-slicer`.

## Výchozí verze

- Zdroj: `HonzaHovora/QAROS_VTK`, větev `main`.
- Commit: `d9d523e6aeb4c95ae237c8bb8fe913ce4b8186a9`.
- Strom: `a739498dca1bdbe7021b2f52819dbc515d5de7ba`.
- Slicer Live plugin a bridge: **r8.4 / ABI 5**.
- QAROS: **0.9.0-dev.3008+65dc9aeb**.
- Aktuální instalátor: `UPDATE-LIVE-PLUGIN.cmd`.
- Aktuální balíček: `downloads/qarvtk-live-dev3008.zip`.
- SHA-256 balíčku:
  `a4977d6291208177155d39ff4ef8708aa4b158116c6037c5ed10ce82d87d266b`.
- Přesná SDK hlavička:
  `ci/sdk-headers/hub-55e6e3f4/include/qar_streaming.h`.
- SHA-256 hlavičky:
  `55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c`.

Převod repozitáře nemění runtime chování ani neoznačuje známé chyby za opravené.
Výchozí strom obsahuje i staré instalační artefakty a prototyp ParaView;
pro aktuální instalaci Sliceru se používá pouze Live balíček dev.3008.
Historie původního mainu je zachována jako druhý rodič importního merge commitu.
Dosud nemergované větve zůstávají ve zdrojovém repozitáři. Připravený ParaView Live plugin je samostatný draft PR #4 v tomto
zdrojovém repozitáři; není součástí výše uvedeného mainu.

## Otevřené problémy

### 1. Nefunkční texty / názvy GUI oken

Nové hlášení od Jana z 22. 10. 2026: „nápisy na GUI oknech nefungují“.
Při předání nejsou k dispozici nový screenshot, log ani přesnější popis této
konkrétní chyby. Není zatím určeno, zda selhává zobrazení `display_name`, jeho
aktualizace, nebo jiný textový prvek GUI. Oprava v tomto předání není.

Relevantní cesta v pluginu:

- `integrations/slicer/qarvtk_host_panels.py`: `panel_title()` sestaví název
  z názvů dat a jména slice view; `SlicePreview` jej při vytvoření předá bridge.
- `src/host_panels.cpp`: `PanelStream::open()` nastavuje
  `QarGuiPanelInit::display_name`; `common_name` je samostatná stabilní identita.
- Panelová video textura záměrně obsahuje obrazový výřez bez rohových anotací.
  Anotace se během streamingu skryjí i ve Sliceru a při Stop se vrátí.
  Očekávaný název má být v titulku QAROS okna, nikoli součástí videa.
- Název se nyní sestavuje při vytvoření panelu. Živé přejmenování existujícího
  panelu je potřeba posoudit zvlášť; není potvrzeno jako příčina hlášeného problému.

### 2. Pád QAROS GUI při otevření panelů

Jan oznámil 22. 10. 2026, že Lukáš potvrdil chybu na straně QAROS a pracuje na
opravě. Dřívější reprodukce: otevření panelů nebo Stop/Start, pád
`qar-stereokit-gui` v `WebCore.dll +0x2DA26E0`, čtení adresy `0x4`, následné
zotavení. R8.4 neobsahuje tvrzení, že tuto upstream chybu opravuje.

Podrobnosti: `docs/PANEL_STARTUP_CRASH_20261019.md` a
`docs/CRASH_REPORT_20261020.md`.

### 3. Opětovné připojení po Stop/Start

Historicky se objevovalo `BackendExhaustedRetriesToCreateStream` a
`no peer acknowledged stream request`. Ruční odstranění a nové vytvoření
receiveru podle Jana obnovilo funkci. Aktuální teardown používá zničení vlastních
SDK handlů, nikoli kill visualiseru. Zbývající stav Stop/Start je potřeba ověřit
na aktuálním QAROS; nelze jej bez další evidence ztotožnit s chybou WebCore.

### 4. Výkon: zatím rozbor architektury

Jan chce nejprve posoudit zapojení a rozsah opakovaného renderování; nová měření
na jeho notebooku při předání nebyla provedena. Slabší notebook je možnost,
nikoli potvrzená příčina.

Původní samostatný HTML rozbor renderu a streamingu je přílohou
[předchozího chatu](https://chatgpt.com/c/6ad6228d-25dc-83ed-8869-53ce94d41654).
Při lokálním předání nebyl celý soubor dostupný, proto není součástí importu.
Repozitář obsahuje [popis architektury](docs/ARCHITECTURE.md) a
[diagnostiku FPS](docs/FPS_DIAGNOSTICS.md).

## Kde začít v kódu

| Oblast | Soubory |
| --- | --- |
| UI a Qt timer | `integrations/slicer/QarosLive.py` |
| GUI poll/render/finish a Stop | `integrations/slicer/qarvtk_host_runtime.py` |
| Původní VTK renderer, kamery očí, hloubka | `integrations/slicer/qarvtk_host_view.py`, `qarvtk_host_gpu.py` |
| SDK session, sendery, native loop | `src/host_bridge.cpp` |
| GPU kopie a synchronizace | `src/gl_copy.cpp`, `src/gl_composite.cpp` |
| Jediný rozpracovaný frame a plánování | `include/qarvtk/host_frame_lease.hpp`, `host_stream_scheduler.hpp` |
| 2D video a GUI panely | `integrations/slicer/qarvtk_host_panels.py`, `src/host_panels.cpp`, `apps/host_panel_config.hpp` |

VTK render obou očí a GPU kopie běží na GUI vlákně. Nativní worker polluje
`try_begin_frame`, předá jeden požadavek GUI, čeká na `finish` a volá
`show_frame`. Jediný rozpracovaný požadavek sdílejí všechny kombinace cíle a
app volumu. Základní jednovolumová cesta používá dvě volání VTK Render na stereo;
doplňková hloubková cesta může použít čtyři. Podrobný výklad v HTML rozlišuje náš
bridge, veřejný SDK kontrakt a interní části QAROS, jejichž zdroje zde nemáme.

## Instalace a sestavení

Uživatelská instalace: zavřít Slicer, spustit `UPDATE-LIVE-PLUGIN.cmd`, při prvním
nasazení přidat `package/windows-start/plugins/slicer` do Additional module paths
a restartovat Slicer. Runtime QAROS se instaluje samostatně; Slicer modul,
bridge a SDK hlavička musí patřit ke kompatibilní verzi.

```powershell
cmake -S . -B build-host -A x64 -DQARVTK_BUILD_RENDERER=OFF -DQARVTK_BUILD_HOST_BRIDGE=ON "-DQAROS_SDK_ROOT=C:/Program Files/Quaternar/QAROS"
cmake --build build-host --config Release --parallel 4
ctest --test-dir build-host -C Release --output-on-failure
```

Další postupy: `docs/INSTALLATION.md`, `docs/WINDOWS.md`,
`docs/QAROS_DEV3008_UPDATE.md`, `docs/SDK_COMPATIBILITY.md`.
Logy lze zabalit pomocí `COLLECT-LOGS.cmd`.

## Dosavadní ověření r8.4

Při přípravě r8.4 prošlo 13 lokálních CTest sad, sestavení proti čtyřem SDK
hlavičkám, kontrola požadovaných dynamických SDK symbolů proti dodané DLL,
kontrola ZIPu a PowerShell fixture instalace/zálohy/rollbacku.
Přesný záznam a omezení jsou v `docs/VALIDATION.md`.

To nenahrazuje běh aktuálního Sliceru s QAROS a brýlemi na Windows. GitHub CI
zdrojového commitu neprošlo; při tehdejší kontrole končily kontrolované joby
ještě před kroky. Důvod nebyl určen. Předání se neopírá o zelené GitHub CI.

Do předání nepatří osobní diagnostické ZIPy, crash dumpy, párovací identity ani
celá dodaná instalace QAROS. Repozitář obsahuje potřebné SDK hlavičky a sestavený
Live bridge v instalačním ZIPu.

## Lokální sestavení na Windows

Dne 22. 10. 2026 byl ze stejného zdrojového commitu sestaven Release host bridge
pomocí Visual Studio 2022 / MSVC 19.39 a SDK z nainstalovaného QAROS.
Build i dočasné soubory byly umístěny na disku D:.
Podrobný postup a výsledek testů jsou v
[LOCAL_WINDOWS_BUILD.md](docs/LOCAL_WINDOWS_BUILD.md).

Toto předání nemění implementaci streamingu. Nefunkční názvy oken, upstream
GUI pád i opětovné připojení po Stop/Start zůstávají otevřené podle popisu výše.
