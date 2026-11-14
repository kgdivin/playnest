<p align="center"><img src="resources/logo.png" width="128" alt="Playnest logo"></p>

# Playnest

*[Version française : README.md](README.md)*

Playnest brings all your Nintendo games together in one app, from the NES to the Switch 2. It installs and updates the emulators for you, tidies your games and protects your saves. The interface is available in English and French.

Playnest contains no emulation code. It drives the best open-source emulators, each maintained by its own team:

| Console | Supported emulators | Install and updates |
|---|---|---|
| NES | Mesen, ares | automatic (GitHub) |
| Super Nintendo | Snes9x, Mesen, ares | automatic (GitHub) |
| Game Boy, Game Boy Color, Game Boy Advance | mGBA, Mesen, ares | automatic (GitHub) |
| Nintendo 64 | Rosalie's Mupen GUI (RMG), ares | automatic (GitHub) |
| Nintendo DS / DSi | melonDS | automatic (GitHub) |
| Nintendo 3DS | Azahar, Citra | Azahar automatic; Citra detected (discontinued) |
| GameCube, Wii | Dolphin | automatic (official website, .7z archive) |
| Wii U | Cemu | automatic (GitHub) |
| Switch | Eden, Ryujinx (Ryubing), Citron, Lichii | Eden, Lichii and Ryujinx automatic; Citron detected |
| Switch 2 | Lichii (very experimental), `.nup`, `.nsp`, `.xci` formats | automatic (GitHub) |
| PC games and fan games | none: launched directly | — |

## Getting started

1. On Windows, unzip `Playnest-windows-x64.zip` into a personal folder, for example `C:\Games\Playnest`. Avoid Program Files.
2. Run `Playnest.exe`. Windows may show "Windows protected your PC" because the app is not signed: click **More info**, then **Run anyway**.
3. On first launch, the welcome screen asks for your language and where your games are. The recommended choice creates a `Nintendo Games` folder in your home folder.
4. Drop your games on the window: each one is recognized, sorted by console and gets its cover.
5. Double-click a game. If its emulator is missing, choose **Install and play**.

Playnest provides no games, keys, BIOS or firmware. Use copies of your own games and the system files dumped from your own consoles.

### Linux
Download `Playnest-linux-x86_64.AppImage` from the [Releases](https://github.com/kgdivin/playnest/releases/latest) page, make it executable (right-click > Properties > Allow executing, or `chmod +x`), then double-click it. It needs a distribution from 2022 or later (Ubuntu 22.04, Fedora 36, Debian 12…).

- Emulators install in one click as AppImages when their team publishes one. Dolphin does not: install it with Flatpak or your distribution, and Playnest finds it.
- Emulators installed with Flatpak, with your distribution, or dropped as AppImages in `~/Applications`, `~/AppImages` or Downloads are detected, along with their saves.
- Windows fan games (`Game.exe`) run through Wine when it is installed.
- Playnest updates itself by replacing its AppImage.

### macOS (beta)
Download `Playnest-macos.dmg` from the [Releases](https://github.com/kgdivin/playnest/releases/latest) page, open it and drag Playnest into Applications. On first launch, right-click Playnest > **Open**, then confirm: the app is not signed with an Apple developer account.

- The macOS version is built and launched automatically on a GitHub Mac for every release, but it has not been tried with real emulators yet: it is a beta.
- Playnest finds the emulators installed in Applications (Dolphin, mGBA, melonDS, Azahar, Cemu, Ryujinx…). Installing them stays manual: the Emulators page opens their official website.
- Works on Intel and Apple Silicon Macs, macOS 12 or later. Controllers are not supported on Mac yet.
- To update, Playnest opens the page of the new version: replace the app in Applications.

## What Playnest does

- **Library.** Games are recognized from their header, not only their extension. Updates and DLC are attached to their game. Covers are downloaded automatically (GameTDB, libretro-thumbnails, Eden boxart), and you can pick your own image.
- **Emulators.** Playnest finds the emulators already on your PC, installs the missing ones from their official sources, and checks for updates every day. Nothing is installed without your click. Before each update your saves are backed up, only the program files are replaced, and **Go back to the previous version** undoes the last update.
- **Update from a file.** Already downloaded a new emulator version? Drop the `.zip`, `.7z`, `.exe` or `.msi` on the window.
- **Saves.** Saves of every emulator, and the `.sav` files next to your games, are backed up every week and before each emulator update. A restore first backs up the current state.
- **Tidy my files.** Sorts games, updates, DLC, emulators and backups into separate folders (`Games\Switch`, `Updates\Switch`, `DLC\Switch`, `Emulators\…`, `Saves`). A preview shows every move first, and the whole tidy-up can be undone.
- **Switch 2.** Lichii `.nup` packages are read directly (title, icon, bundled updates and DLC). Decompressed games (a `base` folder with `exefs` and `romfs`) are named from their title ID.
- **PC games and fan games.** RPG Maker, Pokémon Essentials, Pokémon SDK and RPG Maker MV/MZ games are recognized and launched directly, without an emulator.
- **Controller.** The whole app can be driven with an Xbox or XInput-compatible controller.

## Shortcuts

| Action | Keyboard | Controller (XInput) |
|---|---|---|
| Move | Arrow keys | D-pad or left stick |
| Play / confirm | Enter or double-click | A |
| Back | Esc | B |
| Game menu | Right-click | X |
| Favorite | Ctrl+D | Y |
| Previous / next console | Ctrl+PgUp / Ctrl+PgDn | LB / RB |
| Search | Ctrl+F, or just type | |
| Look for new games | F5 | |
| Fullscreen | F11 | Start |
| Settings | Ctrl+, | |

## Good to know

- **Sending Playnest to a friend.** Give them the zip file (or a copy of the application folder). Your games, settings and saves are not in it: they are kept in `%APPDATA%\Playnest` on your PC.
- **Home.** Playnest opens on the home screen: the game to resume at the top, then the console wall, from the NES (1983) to the Switch 2 (2025). Each console is a tile in its own color with its game count; consoles without games stay visible, dimmed.
- **Language.** Playnest follows the Windows language (French if Windows is in French, English otherwise). To change it, click the **FR / EN** button next to the logo (or **Settings > Launch and display > Language**): Playnest restarts right away in the chosen language. Folders created by "Tidy my files" keep the names chosen at the first tidy-up, even if you change the language later.
- **Coming from Cartouche?** Playnest is the new name of Cartouche. On first launch your library, settings, covers and installed emulators are taken over automatically.
- **Portable mode.** Create a `portable.txt` file next to the executable: everything then stays in `data\`.
- **Modding.** Copy `resources/emulators.json` or `resources/systems.json` into the data folder to add a fork, change arguments or add a console without recompiling. English texts live in `resources/lang/en.json`.

## Building

Qt 6.4 or newer (Core, Gui with private headers, Widgets, Network, Concurrent), CMake 3.21 or newer and a C++17 compiler.

```sh
# Linux
sudo apt install qt6-base-dev qt6-base-private-dev libsdl2-dev cmake ninja-build
cmake -S . -B build -G Ninja && cmake --build build

# Windows (MSVC + official Qt)
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 && cmake --build build
windeployqt build\Playnest.exe
```

## Licenses

- Barlow and Barlow Semi Condensed fonts: SIL Open Font License 1.1.
- Qt 6 is used under LGPLv3. The single-file executable links Qt statically: if you redistribute it, provide the source code too so it can be relinked against another Qt version.
- The emulators, 7-Zip and the Visual C++ runtime belong to their authors. They are downloaded from their official sources, only when you ask.
- "Nintendo" and the names of its consoles and games are trademarks of Nintendo. Playnest is not affiliated with Nintendo.
