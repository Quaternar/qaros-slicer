# QAROS × VTK

Slicer integraci přebírá repozitář **Quaternar/qaros-slicer**.
[Předání vývoje a otevřené chyby](HANDOVER.md) ·
[Lokální sestavení na Windows](docs/LOCAL_WINDOWS_BUILD.md).

Přímý přenos živého obrazu ze Sliceru do QAROS a integrace ParaView.

## Slicer: jeden modul pro přímý streaming

Ve Sliceru používáme **QAROS → QAROS Live Renderer** (`QarosLive.py`).
Obraz vzniká v původním rendereru Sliceru; QAROS dodává pózy očí a přenáší
stereo barvu, hloubku a náhledy řezů. Scéna se neexportuje do náhradního rendereru.
Host bridge nepřidává do Sliceru další VTK ani Qt.

**Od 22. 10. 2026 je starý modul QAROS Streaming (`QarosVTK.py`) odstraněný.**
Nepřebíráme údržbu dalšího rendereru ani kopie funkcí Sliceru. ANARI zůstává
samostatnou alternativou; tento balíček ANARI neinstaluje a tato změna nepřidává
jeho podporu do Live capture. Přímý přenos nadále vyžaduje podporovanou
hostitelskou pipeline, zejména VTK GPU Composite pro CT/MR.

### Instalace nebo aktualizace na Windows

1. Zastav streaming a zavři Slicer.
2. V GitHub Desktop vyber `main`, proveď **Fetch origin → Pull origin**.
3. Spusť **`UPDATE-LIVE-PLUGIN.cmd`**. Funguje i pro první instalaci.
4. Při první instalaci přidej do Slicer **Edit → Application Settings → Modules →
   Additional module paths** složku `package/windows-start/plugins/slicer`
   z tohoto checkoutu a restartuj Slicer.
5. Otevři **QAROS → QAROS Live Renderer**.

Aktualizátor nainstaluje balíček **r8.4** pro kompletní
**QAROS 0.9.0-dev.3008+65dc9aeb**. Instalaci hledá v registru Windows, obvykle
`C:\Program Files\Quaternar\QAROS`. Ověří SDK i obsah ZIPu, zazálohuje původní
soubory a odstraní starý modul včetně exportních helperů a Python cache ze
spravovaných složek. Cesta `package/windows-start` zůstává kvůli existujícím
nastavením Sliceru. Starý `START-PREVIEW.cmd` pouze přesměruje na tento instalátor.
Náhled kostky ani samostatný sender nejsou k instalaci Sliceru potřeba.

Pokud máš další ručně kopírovaný plugin mimo tento checkout, odeber jeho starou
cestu v Additional module paths. Aktualizátor neprohledává cizí složky.

[Instalace a první přenos](docs/INSTALLATION.md) ·
[Postup pro GitHub Desktop](docs/WINDOWS_DESKTOP.md) ·
[Krátký návod](docs/WINDOWS_LIVE_QUICKSTART.txt)

### Přenos

- **Stream to:** Visualizer + headset, Visualizer only nebo Headset only.
  V QAROS spusť receiver nebo připoj brýle a povol přístup Sliceru.
- Viditelné objemy mohou mít každý vlastní pracovní prostor; vypnutím
  **Separate QAROS workspace for each volume** se přenáší společná scéna.
- Výchozí měřítko je **1:1**. Centrovaní nemění velikost; **Model size (at Start)**
  dovoluje 1/8× až 8×. Rozlišení renderu se nastavuje zvlášť.
- Kamera Sliceru nemění natočení dat v QAROS ani při Startu. Model otáčej
  pomocí jeho pracovního prostoru v QAROS; skutečné transformace dat zůstávají živé.
- Obrazové panely mají při Startu úhlopříčku **14″** a poměr stran skutečného
  obrazového výřezu. Ořez na GPU vynechá okolí obrazu; černé části dat zachová.
  Plugin předává název dat do titulku okna; Jan aktuálně hlásí nefunkční texty
  GUI oken — viz [předání](HANDOVER.md). Rohové anotace řezů se po dobu streamování skryjí
  i ve Sliceru a po Stop se obnoví. Velikost panelu lze v QAROS ručně změnit.
- **Stop** uvolní vlastní runtime handle. Před dalším Startem počkej na dokončení.
  Po výměně receiveru stream restartuj. Lukáš potvrdil chybu GUI na straně QAROS
  a pracuje na opravě (zpráva od Jana 22. 10. 2026). R8.4 aktualizuje SDK;
  oprava GUI přijde z QAROS. Problémy s připojením po Stop/Start je potřeba
  ověřit zvlášť.

Po aktualizaci musí modul zobrazovat **Live plugin r8.4**. Samotný Pull
neaktualizuje nainstalované soubory; spusť instalátor a restartuj Slicer.
R8.4 přidává podporu SDK dev.3008 a výběr přijímačů podle jejich přítomnosti
v session. QAROS i přehrávač v brýlích musí používat stejnou verzi; po aktualizaci
může Hub požádat o nové schválení aplikace nebo spárování zařízení.
[Brýle](docs/HEADSET_STREAMING.md) · [Více objemů](docs/MULTIPLE_VOLUMES.md) ·
[Diagnostika FPS](docs/FPS_DIAGNOSTICS.md). Logy zabalíš pomocí `COLLECT-LOGS.cmd`.

## ParaView

Přímý ParaView Live plugin se připravuje ve větvi `codex/paraview-live`,
[draft PR #4](https://github.com/HonzaHovora/QAROS_VTK/pull/4).
Jeho ověření na Windows a s QAROS ještě zbývá. Úklid Sliceru tento PR nemerguje.

Dosavadní exportní prototyp ParaView a společné nástroje pro vývoj zůstávají
oddělené od instalace Sliceru. [Host integrace](docs/HOST_INTEGRATIONS.md).

## Vývoj

Přenosná Python složka obsahuje pouze jeden Slicer modul a privátní knihovny:

```powershell
py -3 scripts/package_plugins.py
```

Pro Slicer transport sestavuj bez samostatného rendereru:

```powershell
cmake -S . -B build-host -A x64 -DQARVTK_BUILD_RENDERER=OFF -DQARVTK_BUILD_HOST_BRIDGE=ON "-DQAROS_SDK_ROOT=C:/Program Files/Quaternar/QAROS"
cmake --build build-host --config Release --parallel 4
ctest --test-dir build-host -C Release --output-on-failure
```

CMake extension je v `integrations/slicer` a registruje jen `QarosLive`.
Staré kombinované instalační ZIPy zůstávají pouze jako historické artefakty;
aktuální instalátor je nepoužívá. Jejich publikování a opětovné balení jsou vypnuté,
protože obsahují odstraněný modul. Pro Slicer použij výhradně aktuální Live balíček.
Samostatný VTK backend slouží zbývajícímu prototypu ParaView a vývojovým testům,
není součástí instalace Sliceru. [Vývoj na Windows](docs/WINDOWS.md).

- [Architektura a rozhodnutí](docs/ARCHITECTURE.md)
- [Historie ověření](docs/VALIDATION.md)
- [Kompatibilita SDK](docs/SDK_COMPATIBILITY.md)

SDK runtime DLL, pozvánky ani párovací identity nejsou součástí repozitáře.
Přesné SDK hlavičky v `ci/sdk-headers/` slouží pro sestavení a testy;
do instalovaného QAROS je nekopíruj.
