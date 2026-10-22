# Instalace přes GitHub Desktop

Příklad používá checkout `D:\QAROS_VTK` a nainstalovaný Slicer.

1. Zastav přenos a zavři Slicer.
2. V GitHub Desktop otevři QAROS_VTK a vyber větev **main**.
3. Klikni **Fetch origin**, potom **Pull origin**.
4. Spusť `D:\QAROS_VTK\UPDATE-LIVE-PLUGIN.cmd`.
5. Instalátor ověří QAROS dev.3008, ZIP a jeho soubory. Nainstaluje jediný modul
   **QAROS Live Renderer** a zazálohuje i odstraní původní exportní modul.
6. Při první instalaci přidej do Slicer **Edit → Application Settings → Modules →
   Additional module paths** cestu
   `D:/QAROS_VTK/package/windows-start/plugins/slicer` a restartuj Slicer.
7. Otevři **QAROS → QAROS Live Renderer**. DLL je
   `D:/QAROS_VTK/package/windows-start/Release/qarvtk-host.dll`, SDK obvykle
   `C:/Program Files/Quaternar/QAROS`.
8. Spusť receiver v QAROS nebo připoj brýle, zvol **Stream to** a klikni
   **Start live streaming**. Případný přístup Sliceru povol v Hubu.

Funguje to i bez dříve rozbaleného Windows starteru. Kompilátor ani samostatný
Python nejsou potřeba. Původní `START-PREVIEW.cmd` nyní spustí stejný instalátor;
nenačítá starý balíček ani náhled kostky.

Záloha je v `package/live-backup-*`. Pokud máš ruční kopii starého pluginu mimo
checkout, odeber její cestu z Additional module paths. V nabídce Sliceru má
zůstat pouze **QAROS Live Renderer**. Privátní složka `QarosVTKLib` je knihovna
pro Live modul a její název neznamená druhý renderer.

[Instalace a řešení potíží](INSTALLATION.md) ·
[Ovládání přenosu](WINDOWS_LIVE_QUICKSTART.txt) · [Brýle](HEADSET_STREAMING.md)
