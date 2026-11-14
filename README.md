<p align="center"><img src="resources/logo.png" width="128" alt="Logo de Playnest"></p>

# Playnest

*[English version: README.en.md](README.en.md)*

Playnest regroupe tous tes jeux Nintendo dans une seule application, de la NES à la Switch 2. Il installe et met à jour les émulateurs à ta place, range tes jeux et protège tes sauvegardes. L'interface existe en français et en anglais.

Playnest ne contient aucun code d'émulation. Il pilote les meilleurs émulateurs open source, chacun maintenu par sa propre équipe :

| Console | Émulateurs pris en charge | Installation et mises à jour |
|---|---|---|
| NES | Mesen, ares | automatiques (GitHub) |
| Super Nintendo | Snes9x, Mesen, ares | automatiques (GitHub) |
| Game Boy, Game Boy Color, Game Boy Advance | mGBA, Mesen, ares | automatiques (GitHub) |
| Nintendo 64 | Rosalie's Mupen GUI (RMG), ares | automatiques (GitHub) |
| Nintendo DS / DSi | melonDS | automatiques (GitHub) |
| Nintendo 3DS | Azahar, Citra | Azahar automatique ; Citra détecté (projet arrêté) |
| GameCube, Wii | Dolphin | automatiques (site officiel, archive .7z) |
| Wii U | Cemu | automatiques (GitHub) |
| Switch | Eden, Ryujinx (Ryubing), Citron, Lichii | Eden, Lichii et Ryujinx automatiques ; Citron détecté |
| Switch 2 | Lichii (très expérimental), formats `.nup`, `.nsp`, `.xci` | automatiques (GitHub) |
| Jeux PC et fan games | aucun : lancés directement | — |

## Démarrage

1. Sous Windows, décompresse `Playnest-windows-x64.zip` dans un dossier personnel, par exemple `C:\Jeux\Playnest`. Évite Program Files.
2. Lance `Playnest.exe`. Windows peut afficher « Windows a protégé votre ordinateur » parce que l'application n'est pas signée : clique sur **Informations complémentaires**, puis **Exécuter quand même**.
3. Au premier lancement, l'écran de bienvenue te demande ta langue et où sont tes jeux. Le choix conseillé crée un dossier `Jeux Nintendo` dans ton dossier personnel.
4. Glisse tes jeux sur la fenêtre : chacun est reconnu, rangé par console et reçoit sa jaquette.
5. Double-clique sur un jeu. Si son émulateur manque, choisis **Installer et jouer**.

### Linux
1. Télécharge `Playnest-linux-x86_64.AppImage` depuis la page [Releases](https://github.com/kgdivin/playnest/releases/latest).
2. Rends le fichier exécutable : clic droit > Propriétés > Autoriser l'exécution, ou `chmod +x Playnest-linux-x86_64.AppImage`.
3. Double-clique dessus. Rien à installer ; il faut une distribution de 2022 ou plus récente (Ubuntu 22.04, Fedora 36, Debian 12…).

- Les émulateurs s'installent en un clic au format AppImage quand leur équipe en publie une. Dolphin n'en publie pas : installe-le par Flatpak ou par ta distribution, Playnest le retrouve.
- Les émulateurs installés par Flatpak, par ta distribution, ou posés en AppImage dans `~/Applications`, `~/AppImages` ou Téléchargements sont reconnus, avec leurs sauvegardes.
- Les fan games Windows (`Game.exe`) se lancent avec Wine s'il est installé.
- Playnest se met à jour tout seul en remplaçant son AppImage.

### macOS (bêta)
1. Télécharge `Playnest-macos.dmg` depuis la page [Releases](https://github.com/kgdivin/playnest/releases/latest), ouvre-le et glisse Playnest dans Applications.
2. Au premier lancement, fais un clic droit sur Playnest > **Ouvrir**, puis confirme : l'application n'est pas signée par un compte développeur Apple.

- La version macOS est compilée et lancée automatiquement sur un Mac de GitHub à chaque version, mais elle n'a pas encore été essayée avec de vrais émulateurs : c'est une bêta.
- Playnest retrouve les émulateurs installés dans Applications (Dolphin, mGBA, melonDS, Azahar, Cemu, Ryujinx…). Leur installation reste manuelle : la page Émulateurs ouvre leur site officiel.
- Fonctionne sur les Mac Intel et Apple Silicon, à partir de macOS 12. La manette n'est pas encore gérée sur Mac.
- Pour mettre à jour, Playnest ouvre la page de la nouvelle version : remplace l'application dans Applications.

### Envoyer Playnest à un ami
Donne-lui le lien de la page [Releases](https://github.com/kgdivin/playnest/releases/latest) : il y prend le fichier pour Windows, Linux ou macOS. Tu peux aussi lui envoyer directement le zip. Tes jeux, tes réglages et tes sauvegardes ne sont pas dedans : ils sont gardés dans `%APPDATA%\Playnest` sur ton PC. Ton ami démarre avec l'écran de bienvenue, dans sa langue.

### Tu utilisais Cartouche ?
Playnest est le nouveau nom de Cartouche. Au premier lancement, ta bibliothèque, tes réglages, tes jaquettes et tes émulateurs installés sont repris automatiquement (le dossier `%APPDATA%\Cartouche` devient `%APPDATA%\Playnest`). Ton dossier `Jeux Nintendo` ne bouge pas. Tu peux supprimer l'ancien `Cartouche.exe`.

### Accueil
Playnest s'ouvre sur l'accueil : le jeu à reprendre en haut, puis le mur des consoles, de la NES (1983) à la Switch 2 (2025). Chaque console est une tuile à sa couleur avec son nombre de jeux ; un clic ouvre sa bibliothèque. Les consoles sans jeu restent affichées, éteintes. Toutes les consoles sont aussi dans la barre de gauche, quelle que soit la taille de la fenêtre.

### Langue
Playnest suit la langue de Windows : français si Windows est en français, anglais sinon. Pour la changer, clique sur le bouton **FR / EN** à côté du logo (ou **Paramètres > Lancement et affichage > Langue**) : Playnest se relance aussitôt dans la langue choisie. Les dossiers créés par « Ranger mes fichiers » gardent les noms choisis au premier rangement (`Jeux`, `Mises à jour`… ou `Games`, `Updates`…), même si tu changes de langue ensuite.

## Ce que fait Playnest

### Émulateurs
- **Détection automatique.** Playnest reconnaît les émulateurs déjà installés, leur version (même pour un dossier comme `Eden-Windows-v0.2.1-amd64-gcc-standard`) et leur dossier de données. Pour Lichii, la version (par exemple `build-150`) est lue directement dans `lichii.exe`, et Playnest suit son dossier de données : `%APPDATA%\lichii` pour les builds récentes, `%APPDATA%\eden` pour les plus anciennes.
- **Choix automatique de l'émulateur.** Par défaut, chaque console utilise l'émulateur installé le plus adapté. Par exemple, la 3DS utilise Citra tant qu'Azahar n'est pas installé. Tu peux imposer ton choix par console, ou par jeu.
- **Mises à jour sûres :**
  - vérification quotidienne, avec un badge sur « Émulateurs » quand une mise à jour est disponible ;
  - rien n'est installé sans ton clic ;
  - avant chaque mise à jour, tes sauvegardes sont copiées ;
  - seuls les fichiers du programme sont remplacés : réglages, clés, firmware et dossiers `user/`, `portable/`, `config/` et assimilés ne sont jamais écrasés ;
  - les fichiers remplacés sont gardés : **Revenir à la version précédente** annule la dernière mise à jour ;
  - la mise à jour est refusée si l'émulateur est ouvert, ou si son dossier est protégé en écriture ;
  - la « saveur » installée est conservée : un Eden GCC est mis à jour vers un Eden GCC.
- **Mise à jour depuis un fichier.** Tu as déjà téléchargé la nouvelle version ? Glisse le fichier sur la fenêtre de Playnest, ou clique sur **Depuis un fichier…** dans la carte de l'émulateur. Ton fichier n'est jamais supprimé.
  - **Archive** (`.zip`, `.7z`) : Playnest reconnaît l'émulateur d'après son exécutable et applique les mêmes protections qu'une mise à jour automatique (copie des sauvegardes, réglages et clés conservés, retour arrière possible).
  - **Installateur officiel** (`.exe` ou `.msi`, par exemple `azahar-windows-msys2-2126.2-installer.exe`) : Playnest copie tes sauvegardes, ouvre l'installateur, attend qu'il ait fini, puis retrouve tout seul la nouvelle version. Le retour arrière n'est pas possible dans ce cas.
  - **Application seule** (`azahar.exe`, par exemple) : seul l'exécutable est remplacé, avec retour arrière possible.
- **Fichiers système.** Chaque émulateur affiche l'état de ses clés et de son firmware (✓ ou ✗). **Importer…** copie ton `prod.keys`, `title.keys`, `aes_keys.txt` ou `keys.txt` au bon endroit, et l'ancien fichier est gardé en `.bak`. Playnest ne télécharge jamais de clés, de BIOS ni de firmware.

### Switch 2 et paquets `.nup`
- Les paquets `.nup` de Lichii sont lus directement : nom du jeu (en français s'il existe), icône, version, ainsi que les mises à jour et DLC qu'ils contiennent (« Dans ce fichier : mise à jour 2.0.0 incluse, 2 DLC inclus »).
- Les « Nintendo Switch 2 Edition » (par exemple Pokémon Legends Z-A) gardent le title ID `0100…` du jeu Switch, mais leur programme est un programme Switch 2. Playnest lit le NPDM du `.nup` (comme Lichii) et range ces versions dans **Switch 2**, à côté de la version Switch. Pour un `.nsp` ou `.xci` chiffré, « Switch 2 » dans le nom du fichier sert d'indice. Les mises à jour et DLC Switch se rattachent à la version Switch.
- Les jeux Switch 2 décompressés (un dossier `base` avec `exefs` et `romfs`, parfois `updates` à côté) n'ont pas de nom intégré. Playnest reconnaît les principaux jeux Switch 2 à leur title ID (Mario Kart World, Donkey Kong Bananza, Kirby Air Riders…), sinon il prend le nom du dossier qui les contient.
- Un `.nup` se lance toujours avec Lichii, même si la console utilise un autre émulateur par défaut : Eden, Citron et Ryujinx ne savent pas ouvrir ce format, et ils apparaissent grisés dans le choix de l'émulateur.

### DLC et mises à jour
- Les mises à jour et DLC sont reconnus par leur title ID (`…800` pour une mise à jour, `…001`, `…002` pour les DLC) ou par leur nom (`[DLC]`, `[Update v4.0.0]`, `[UP v2.1.2]`, `[Up v4.0.1]`…), puis rattachés à leur jeu, même quand le nom du jeu n'a pas d'identifiant.
- Dans la grille, chaque jeu porte des pastilles « MàJ 4.0.1 » et « +2 DLC ». Le panneau de détails liste ses contenus ; un clic mène au contenu, et inversement.
- La catégorie **DLC et mises à jour** les montre tous à part, avec leur nom (« The Crown Tundra », « Mise à jour 2.0.2 ») et le jeu auquel ils appartiennent.
- Pour les utiliser avec Eden, Citron, Ryujinx ou Lichii : dans l'émulateur, menu Fichier puis « Installer des fichiers dans la NAND ». Les `.nup` n'en ont pas besoin.

### Jeux PC et fan games
Les fan games qui se lancent sans émulateur (Pokémon Z : La Colère de Zygarde et les autres jeux RPG Maker, Pokémon Essentials, Pokémon SDK, RPG Maker MV/MZ…) ont leur propre catégorie, **Jeux PC et fan games**.
- Ils sont reconnus tout seuls dans tes dossiers de jeux : Playnest lit leur titre (`Game.ini`, `package.json`) et prend leur écran titre comme image.
- Pour un autre jeu (Unity, Godot…), glisse son `.exe` ou son dossier sur la fenêtre, ou utilise **+ > Ajouter un jeu PC ou un fan game…**. Il reste où il est, et se lance directement depuis son dossier, avec le temps de jeu compté.
- « Ranger mes fichiers » déplace chaque fan game avec tout son dossier (sauvegardes comprises) dans `Jeux\PC`, sauf ceux installés dans Program Files.

### Ajouter de nouveaux jeux
Glisse tes nouveaux jeux (fichiers, dossier entier, ou jeu Switch décompressé avec son dossier `base`) sur la fenêtre de Playnest, ou clique sur **+** en haut de la bibliothèque. Playnest reconnaît chaque fichier et le range directement au bon endroit : un jeu Switch dans `Jeux\Switch`, une mise à jour dans `Mises à jour\Switch`, un DLC dans `DLC\Switch`, un `.nup` dans `Jeux\Switch 2`… Tu choisis **Déplacer** (instantané sur le même disque) ou **Copier** (l'original reste en place, pratique depuis une clé USB). Le jeu apparaît aussitôt dans la bibliothèque.

Tu peux aussi poser un fichier toi-même n'importe où dans `Jeux Nintendo`, puis appuyer sur F5.

### Ranger mes fichiers
Le bouton **Ranger mes fichiers** sépare tout dans des dossiers distincts, par défaut dans `Jeux Nintendo` de ton dossier personnel :

```
Jeux Nintendo\
  Jeux\Switch\, Jeux\Switch 2\, Jeux\DS\…
  Mises à jour\Switch\
  DLC\Switch\
  Émulateurs\Eden\, Émulateurs\Citra\…
  Sauvegardes\        (copies faites par Playnest)
```

- Un aperçu montre chaque fichier, son ancien dossier et sa taille avant de commencer.
- Sur le même disque, chaque déplacement est un simple renommage, donc instantané, même pour un `.nup` de 25 Go. Vers un autre disque, le fichier est copié, vérifié, puis l'original est supprimé ; la place libre est contrôlée avant.
- Les jeux gardent leur temps de jeu, leurs favoris et leur jaquette. Les `.sav` posés à côté d'une ROM DS ou N64 suivent le jeu.
- Les émulateurs sont déplacés avec leur dossier `user/`, et Playnest met à jour leur chemin. Un émulateur ouvert, installé dans Program Files ou posé directement dans Téléchargements reste en place.
- Les dossiers devenus vides sont supprimés, et les dossiers de jeux sont mis à jour. Si tu les avais épinglés dans l'Explorateur Windows, retire ces raccourcis et épingle `Jeux Nintendo` à la place.
- Tout est consigné : **Tout remettre comme avant** replace chaque fichier à son ancien emplacement, même plus tard.

### Outils nécessaires
- **Runtime Visual C++ 2015-2022.** Playnest vérifie qu'il est présent et en version 14.40 ou plus : les émulateurs récents plantent avec une version plus ancienne. Il l'installe depuis Microsoft si besoin.
- **7-Zip.** Il est téléchargé automatiquement quand une archive `.7z` doit être extraite (Dolphin).
- **Vulkan.** Playnest vérifie la version disponible et la carte graphique utilisée.

### Sauvegardes
- Il copie dans une archive datée (`data\backups`, ou `Sauvegardes` après un rangement) les sauvegardes de chaque émulateur, ainsi que les `.sav` posés à côté des jeux (DS, N64, Game Boy, Game Boy Advance…).
- Les copies sont automatiques avant chaque mise à jour et chaque semaine. Seules les N dernières copies automatiques sont gardées, et les copies manuelles ne sont jamais supprimées.
- Pour une restauration, tu choisis ce que tu remets en place. L'état actuel est d'abord copié par sécurité, et aucun fichier n'est supprimé.

### Bibliothèque
- Les jeux sont reconnus par leur en-tête, pas seulement par leur extension :
  - GameCube et Wii sont distingués, de même que Switch (`0100…`) et Switch 2 (`0400…`), ou Game Boy et Game Boy Color ;
  - les mises à jour et DLC sont masquées ;
  - les jeux Wii U décryptés et les ExeFS extraits sont pris en charge.
- Le scan est incrémental : après le premier passage, seuls les fichiers nouveaux ou modifiés sont relus.
- Les jaquettes sont téléchargées automatiquement (GameTDB, libretro-thumbnails, boxart d'Eden), pour toutes les consoles. Quand le nom du fichier ne correspond pas exactement, Playnest cherche le titre le plus proche dans la liste des jaquettes (un nom avec une apostrophe typographique ou un tag de site, un hack N64 qui prend la jaquette du jeu d'origine, une Switch 2 Edition qui prend celle du jeu Switch). Tu peux aussi choisir ta propre image avec **Jaquette…**.
- Le bandeau **Reprendre** affiche en grand le dernier jeu lancé, sur un fond tiré de sa jaquette.
- Tu as aussi les favoris, les jeux récents, le temps de jeu, la recherche sans accents, le plein écran et la navigation à la manette.

### Application
- Playnest se met à jour depuis tes releases GitHub (**Paramètres > Mises à jour**). Le dossier `data` et le mode portable ne sont jamais touchés, et tout est annulé si la copie échoue.
- Une seule instance est ouverte à la fois, la bibliothèque est enregistrée de façon atomique, et un journal est écrit dans `data\playnest.log`.
- Les données de Cartouche et d'Omnia, les anciens noms de l'application, sont reprises automatiquement.

## Raccourcis

| Action | Clavier | Manette (XInput) |
|---|---|---|
| Se déplacer | Flèches | Croix ou stick gauche |
| Jouer / valider | Entrée ou double-clic | A |
| Retour | Échap | B |
| Menu du jeu | Clic droit | X |
| Favori | Ctrl+D | Y |
| Console précédente / suivante | Ctrl+PgPréc / Ctrl+PgSuiv | LB / RB |
| Rechercher | Ctrl+F, ou taper directement | |
| Rechercher les nouveaux jeux | F5 | |
| Plein écran | F11 | Start |
| Paramètres | Ctrl+, | |

## Pour les moddeurs

- **Mode portable.** Crée un fichier `portable.txt` à côté de l'exécutable : tout reste alors dans `data\` (bibliothèque, jaquettes, émulateurs installés, copies de sauvegardes, outils). Dans ce mode, garde le dossier `data` quand tu remplaces Playnest par une nouvelle version.
- **Traduire.** Les textes du code sont en français ; l'anglais est un dictionnaire, `resources/lang/en.json`. `tools/extract_strings.py . resources/lang/en.json` liste les textes qui n'ont pas encore de traduction.
- **Tout est en JSON.** Copie `resources/emulators.json` ou `resources/systems.json` dans le dossier de données pour ajouter ton fork, changer des arguments, des chemins de données, la source des mises à jour (`github`, `gitlab` ou `page` web) ou les fichiers système attendus. Les entrées qui ont le même `id` remplacent celles d'origine. Le champ `unsupported` liste les extensions qu'un émulateur ne sait pas ouvrir (par exemple `["nup"]`) : Playnest choisit alors un autre émulateur pour ces fichiers.
- **Arguments par émulateur.** Ils se règlent dans la page Émulateurs, avec les jetons `{rom}`, `{romdir}`, `{romname}`, `{emudir}` et `{system}` (nom de la console pour l'émulateur, utile à ares).

## Compiler

Il faut Qt 6.4 ou plus récent (modules Core, Gui avec en-têtes privés, Widgets, Network, Concurrent), CMake 3.21 ou plus et un compilateur C++17.

```sh
# Linux
sudo apt install qt6-base-dev qt6-base-private-dev libsdl2-dev cmake ninja-build
cmake -S . -B build -G Ninja && cmake --build build

# Windows (MSVC + Qt officiel)
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 && cmake --build build
windeployqt build\Playnest.exe

# Depuis Linux vers Windows : un seul .exe, avec Qt compilé en statique pour MinGW-w64
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-cross.cmake \
      -DCMAKE_PREFIX_PATH=/opt/qt6-win-static -DQT_HOST_PATH=/usr
```

**GitHub Actions** : à chaque envoi, `.github/workflows/build.yml` compile Playnest pour Linux (AppImage), Windows (zip) et macOS (dmg), lance les tests de détection, puis lance réellement l'application sur chaque système et vérifie qu'elle affiche sa fenêtre.

**Publier une version** : change le numéro de version dans `CMakeLists.txt`, décris les nouveautés dans `RELEASE_NOTES.md` et envoie sur `main`. Si cette version n'a pas encore été publiée, le workflow crée la release avec les trois fichiers. Playnest la détecte et propose la mise à jour à tous ceux qui l'utilisent.

**Tests** (avec `-DPLAYNEST_BUILD_TESTS=ON`) :
- `tests/make_samples.py` crée des en-têtes de jeux synthétiques (aucun contenu de jeu).
- `playnest_detect_test <dossier>` vérifie la détection et le lancement.
- `playnest_detect_test --organize-test <copie des échantillons>` range une copie des échantillons, vérifie la bibliothèque, puis annule le rangement.
- `playnest_detect_test --install-test <dossier d'émulateur>` vérifie, contre un serveur local, la mise à jour, la copie des sauvegardes, le retour arrière et la restauration.

## Licences

- Polices Barlow et Barlow Semi Condensed : SIL Open Font License 1.1.
- Qt 6 est utilisé sous LGPLv3. L'exécutable « un seul fichier » lie Qt statiquement : si tu le redistribues, fournis aussi le code source pour qu'il puisse être relié à une autre version de Qt.
- Les émulateurs, 7-Zip et le runtime Visual C++ appartiennent à leurs auteurs. Ils sont téléchargés depuis leurs sources officielles, uniquement à ta demande.
- « Nintendo », les noms de consoles et de jeux sont des marques de Nintendo. Playnest n'est pas affilié à Nintendo. Utilise uniquement des jeux et des fichiers système issus de tes propres consoles et cartouches.
