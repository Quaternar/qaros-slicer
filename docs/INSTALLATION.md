# Instalace QAROS Live do Sliceru

Slicer má jeden QAROS modul: **QAROS Live Renderer**. Přenáší hotový obraz
z rendereru Sliceru. Starý **QAROS Streaming** se samostatným rendererem byl
odstraněn. ANARI je samostatná alternativa, kterou tento instalátor nemění.

## Co potřebuješ

- Windows x64 se Slicerem; referenční hostitelské testy používají Slicer 5.12.4.
- Kompletní QAROS **0.9.0-dev.3008+65dc9aeb**, včetně `include/qar_streaming.h`
  a původní složky `bin`, s podporovanou GPU. Přehrávač v brýlích musí mít
  stejnou verzi; pokud Hub požádá o nové spárování/schválení, dokonči ho.
- Checkout `HonzaHovora/QAROS_VTK`, větev `main`, například `D:\QAROS_VTK`.

Pro instalaci hotového balíčku nepotřebuješ Python, VTK, vcpkg ani kompilátor.
Použije se Python a renderer dodaný se Slicerem. Nepřepisuj jednotlivé soubory SDK.

## První instalace i aktualizace

1. Zastav streaming a zavři Slicer, aby nedržel DLL.
2. V GitHub Desktop proveď **Fetch origin → Pull origin** na větvi `main`.
3. Spusť `D:\QAROS_VTK\UPDATE-LIVE-PLUGIN.cmd`.
4. Skript najde QAROS v registru, případně v `C:\Program Files\Quaternar\QAROS`.
   Ověří SDK, ZIP a jednotlivé soubory; připraví
   `D:\QAROS_VTK\package\windows-start` i bez předchozí instalace.
5. Spusť Slicer. V **Edit → Application Settings → Modules → Additional module
   paths** přidej `D:/QAROS_VTK/package/windows-start/plugins/slicer`, pokud tam
   ještě není. Potom Slicer restartuj.
6. V nabídce **QAROS** otevři **QAROS Live Renderer**.

Pro nestandardní umístění QAROS lze instalátor spustit z PowerShellu:

```powershell
.\UPDATE-LIVE-PLUGIN.cmd -SdkRoot 'D:\moje-kompletni-instalace-QAROS'
```

Předchozí soubory ukládá do `package/live-backup-*`. Při chybě instalace obnoví
původní soubory. Starý `QarosVTK.py`, jeho exportní helpery a import cache
odstraní ze `package/windows-start/plugins/slicer` a případné složky
`package/QAROS-VTK-plugins/slicer`. Soukromé nastavení ani jiná rozšíření nemaže.

Pokud po restartu stále vidíš **QAROS Streaming**, máš jeho další kopii v jiné
cestě. Odeber cestu ke starému balíčku z Additional module paths; ponech novou
složku výše. Záložní složky nepřidávej mezi moduly.

Starý `START-PREVIEW.cmd` je pouze kompatibilní zkratka k instalátoru Live.
Staré kombinované ZIPy ani náhled kostky pro tento postup nepoužívej.

## První přenos

1. Načti scénu do Sliceru. U CT/MR zapni **Volume Rendering**, zvol **VTK GPU /
   Composite** a ověř obraz v prvním 3D pohledu. ROI a presety mohou zůstat živé.
2. V QAROS spusť jeden receiver Visualizeru; pro brýle připoj headset.
3. V QAROS Live Renderer ověř **Host bridge DLL**:
   `D:/QAROS_VTK/package/windows-start/Release/qarvtk-host.dll`
   a **QAROS SDK folder**: obvykle `C:/Program Files/Quaternar/QAROS`.
4. Vyber **Stream to**: Visualizer only, Headset only nebo Visualizer + headset.
5. Klikni **Start live streaming**. Případný přístup aplikace Slicer povol v Hubu.
6. Změny scény se přenášejí živě. **Stop live streaming** uvolní vlastní
   prostředky; před restartem počkej na opětovné povolení Start.

**Model size = 1×** zachovává fyzické měřítko 1:1. Centrovaní scénu pouze
vystředí. Násobek 1/8× až 8× i nové rozměry pracovního prostoru se použijí
při Start. Náhledy viditelných řezů ve Four-up mají při vytvoření úhlopříčku 14″.
Podrobnosti: [rychlý návod](WINDOWS_LIVE_QUICKSTART.txt),
[brýle](HEADSET_STREAMING.md), [více objemů](MULTIPLE_VOLUMES.md).

Aktuální Live capture očekává podporovanou VTK GPU pipeline. Tato změna
nepřidává automatické zachycení libovolného ANARI rendereru.

## Vývoj a jiné balení

`py -3 scripts/package_plugins.py` vytvoří `package/QAROS-VTK-plugins/slicer`
a ZIP Python modulů. Slicer složka obsahuje pouze `QarosLive.py` a jeho privátní
knihovny. Při opakovaném balení odstraní starý exportní modul a jeho cache.
Tento Python ZIP neobsahuje nativní DLL ani SDK; není to balíček pro Extensions
Manager. `integrations/slicer/CMakeLists.txt` slouží k sestavení standardní
extension s vývojovým buildem Sliceru a registruje jen Live modul.

Host DLL se sestavuje s `QARVTK_BUILD_RENDERER=OFF` a
`QARVTK_BUILD_HOST_BRIDGE=ON`; postup je v README. Není třeba kompilovat ani
udržovat samostatný renderer kvůli Sliceru.

ParaView má vlastní postup v [HOST_INTEGRATIONS.md](HOST_INTEGRATIONS.md).
Jeho nový Live plugin se připravuje v [PR #4](https://github.com/HonzaHovora/QAROS_VTK/pull/4).

## Když se něco nepovede

Aktuální modul ukazuje **Live plugin r8.4**; najetím na tento řádek zjistíš
cestu skutečně načteného souboru. Po Pull vždy spusť `UPDATE-LIVE-PLUGIN.cmd`
se zavřeným Slicerem. Nastavení 14″ je od r8.3 předané explicitně i již
existujícímu panelu. U náhledů se skrývají rohové anotace řezů i ve Sliceru;
po Stop se obnoví. Podrobnosti jsou v [popisu aktualizace](QAROS_DEV3008_UPDATE.md).

| Projev | Co ověřit |
| --- | --- |
| Modul není v nabídce | Celá složka pluginu v Additional module paths, restart Sliceru. |
| Vidíš i starý QAROS Streaming | Odeber cestu k jiné staré kopii pluginu, nepřidávej zálohu. |
| SDK / DLL mismatch | Kompletní QAROS dev.3008 a DLL z téhož Live balíčku; nemíchej SDK soubory. |
| Start bez obrazu | Receiver nebo připojený headset, správný Stream to, schválení v Hubu. |
| Po Stop/Start není obraz | Ulož logy aktuálního pokusu. Dřívější workaround byl vytvořit nový receiver a stream restartovat; oprava v dev.3008 zatím není ověřena. |
| Nepodporovaný mapper nebo režim objemu | Pro Live CT/MR použij podporovaný VTK GPU Composite; ověř nejdřív jednoduchou scénu. |

Log folder ukazuje záznamy pluginu; `COLLECT-LOGS.cmd` je zabalí k diagnostice.
Při chybě pošli přesnou hlášku, verzi Sliceru/QAROS a logy.
