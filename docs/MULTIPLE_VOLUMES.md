# Samostatný pracovní objem pro každá data

Experiment na větvi `main` rozšiřuje r17 o vlastní
QAROS app volume a stereo sender pro každý viditelný GPU objem z prvního 3D
pohledu Sliceru. Všechny používají jednu SDK relaci a jedno pozvání vieweru.
Kostky mají různá ID, vlastní umístění a lze s nimi manipulovat samostatně.

## Oprava startu po testu 16. října

První Windows pokus s nezávislými kostkami vytvořil sender pro MRHead, ale
druhý sender pro MRBrainTumor1 skončil na `QueryMatchingStream` timeoutu po
přibližně 51 sekundách. Původní implementace čekala na všechny sendery před
odesláním prvního snímku, takže odeslala nula snímků a při chybě zavřela relaci.
V záznamech Windows je starší pád Sliceru ze 14:23; aktuální běh z 15:07 skončil
zaznamenaným timeoutem a řízeným ukončením transportu.

Build **independent-startup-r2** nejprve zveřejní všechny pracovní objemy a
negociaci senderů přesune na samostatné vlákno. Připravený sender posílá snímky
okamžitě, i když další stále čeká. Selhání připojení jedné kostky se vypíše do
statusu a neukončí již připravené streamy. Stop zruší čekající připojení a před
zrušením relace počká na jeho dokončení. Log obsahuje ID kostek a `FIRST_FRAME`
pro každý sender; `COLLECT-LOGS.cmd` nově přibalí i SDK logy z adresářů běhů
a `HOST_UPDATE_INFO.json` se skutečnou verzí experimentální DLL.

Regresní test simuluje příjemce, který nepotvrdí druhý stream, dokud první
nepošle obraz, a samostatný timeout druhého streamu. Příčina timeoutu uvnitř
SDK/vieweru ale ze starého ZIPu není prokázaná: oprava blokování neprokazuje,
že instalovaný Visualizer zvládne oba streamy. To vyžaduje nový cílový pokus.

## Vyzkoušení na Windows

1. Zastav streaming a zavři Slicer.
2. V GitHub Desktop přepni na `main` a dej Fetch/Pull.
3. Spusť `UPDATE-LIVE-PLUGIN.cmd`. Ověří přibalený ZIP a aktualizuje Python
   plugin i `package/windows-start/Release/qarvtk-host.dll` na ABI 5.
   Instalátor funguje i bez předchozího balíčku r17.
4. Otevři Slicer. V QAROS Live Renderer nech zapnuté
   **Separate QAROS workspace for each volume** (výchozí stav).
5. V prvním 3D pohledu zobraz oba objemy přes **VTK GPU Ray Casting** a
   **Composite with shading**. Data mohou být i na stejných souřadnicích.
6. Dej Start. Očekávaný výsledek jsou dvě pracovní kostky vedle sebe,
   každá pouze se svými daty. Vyzkoušej přesun/natočení jedné v QAROS,
   změnu presetu a ROI pouze jednoho objemu ve Sliceru a Stop/Start.

Použij kompletní instalované QAROS dev.3008, obvykle `C:/Program Files/Quaternar/QAROS`. Nový plugin vyžaduje přibalenou DLL ABI 5;
starou DLL odmítne s vysvětlením. Pokud máš v poli **Host bridge DLL** vlastní
cestu, vyber aktualizovanou DLL z `package/windows-start/Release`.

`START-PREVIEW.cmd` nyní přesměruje na instalátor Live; původní r17 neobnovuje.

## Chování

- Každý objem dostane jméno odpovídající datům ve Sliceru (případně `Slicer volume N`).
- **Center model in workspace at Start** vystředí data bez změny velikosti.
  V r8 je výchozí měřítko 1:1; **Model size (at Start)** nabízí 1/8× až 8×,
  stejný násobek pro všechny objemy, aby zůstal zachovaný jejich poměr velikostí.
  Kostky se rozmístí vedle sebe s mezerou 12 cm. Řádek Workspace vypíše rozměry jednotlivě.
- Data, transfer funkce, transformace a ROI zůstávají původní živé VTK objekty.
  Během zachycení se dočasně skryje ostatní obsah; desktop se pak obnoví.
  Povrchy, segmentace a dekorace nejsou v samostatných kostkách přiřazovány k datům.
- Skrytí zdrojového objemu vyprázdní jeho kostku; ostatní data do ní nepřeskočí.
  Po přidání, odebrání či výměně rendereru objemu je potřeba Stop/Start.
  Odebrání zdroje za běhu stream ukončí s vysvětlením. Rozměry kostek se určují při Start.
- Náhledy řezů tvoří jednu společnou řadu pod skupinou kostek.
- Vypnutí **Separate QAROS workspace for each volume** zachovává společný
  pracovní objem s celou scénou, včetně neprůhledných povrchů.
- Speciální fúzní renderer **VTK Multi-Volume** zůstává nepodporovaný.

## Přenos a výkon

Každá kostka má vlastní `QarAppVolumeId`, render sender, SDK oční pózy a sdílený
WGL kontext. Připojování běží odděleně od zpracování snímků. GUI zpracovává jeden snímek v daný okamžik a střídá připravené
sendery. Čekající nebo reconnectující sender neblokuje snímky ostatních.
Neúspěšné připojení je viditelné ve statusu, ostatní kostky pokračují.
Zastavení zruší probíhající připojení a uvolní všechny sendery a kostky před zánikem relace.

Pro oddělené objemy se standardně použije rychlá cesta: dva renderovací průchody
na stereo snímek **každé kostky**, bez doplňkového průchodu hloubky. Více streamů
ale vyžaduje více renderování a enkódování. Začni se dvěma objemy a případně
vypni slice panely nebo sniž Render resolution. Limity a FPS musí ověřit cílová
NVIDIA karta. Diagnostika `volume_frames`/`volume_fps` rozlišuje jednotlivé kostky;
celkový počet snímků v statusu je součet za všechny streamy.

Společná víceobjemová scéna používá nezměněnou barvu Sliceru a doplňkový průchod
nejbližší hloubky pro každé oko, standardně s polovičním rozlišením hloubky.
Vyžaduje VTK 9.6, Composite a standardní pipeline bez SSAO/stínů/konfliktního
shaderu. Každý pixel nese jednu hloubku, ne všechny průhledné vrstvy.

## Ověření a sestavení

Prošlo skutečné GPU renderování ve VTK 9.6.2 přes EGL/Mesa: oddělená barva
porovnaná s nativním renderem každého objemu, hloubka s nezávislým VTK
RenderToImage, vlastní bounds, obnovení desktopu a viditelnosti, skrytí/smazání
zdroje a chyba při předání snímku. Prošla také původní společná víceobjemová cesta,
Python testy směrování snímků/ukončení a C++ test střídání senderů.

Přibalená Windows x64 DLL byla sestavena z tohoto zdroje pomocí MinGW-w64/GCC
13.2 (Ubuntu noble); C++ runtime je přilinkovaný staticky a importy jsou pouze
standardní systémové knihovny Windows. Kompilace ověřuje velikosti ABI struktur.
Reprodukovat ji lze přes `scripts/build_host_cross.sh`; standardní MSVC build
zůstává dostupný přes CMake s `QARVTK_BUILD_HOST_BRIDGE=ON`.

Plné spuštění v Sliceru na Windows, NVIDIA enkodéry a manipulace více kostkami
ve Visualizeru/Questu zatím v tomto prostředí ověřené nejsou. Nový Windows ABI
smoke test je připravený pro CI. Dřívější GitHub Actions končily před prvním krokem
bez runneru; přibalená DLL na jejich dokončení nezávisí.
