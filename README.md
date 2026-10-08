# Impera — An Ultima 5 Engine

**Impera** is an enhanced engine port of **Ultima 5: Warriors of Destiny**,
in the tradition of [Exult](https://exult.sourceforge.io/),
[Nuvie](https://nuvie.sourceforge.net/), and [Pentagram](https://pentagram.sourceforge.net/).
It runs the original game data on modern systems while adding optional graphics,
input, audio, and save-management improvements.

Impera uses **[u5d — Ultima V Decompilation Project](https://github.com/wonst719/u5d)**
by **wonst719** as its base. Credit for the original decompilation and reconstructed
game engine belongs to that project.

You must supply your own Ultima 5 game files. Game data and optional music are
external and are not bundled into the engine. The build still produces an
executable named `ultima5`, and environment variables retain the `U5D_` prefix.

> **Want music? Download the Ultima 5 MP3 music from
> [Exodus — Downloads](https://exodus.voyd.net/downloads/), extract the archive,
> and select the folder containing the MP3 files. Music is not bundled with
> Impera; without player-supplied music files, the game has no music.
> If you skipped music at first launch, open **Engine Options**, switch **Music**
> on (or off and then on if already enabled), and choose your music folder in the **Ultima-themed, pixelated
> folder browser**. Open a directory, then choose **Select this folder**.
> Once a music folder is configured, Music simply toggles playback on and off.
> No restart is required.

## Enhanced features

- Fullscreen overhead maps that show more of the world, with crisp integer-scaled pixels.
- Mouse movement, contextual double-click actions, single-click direction selection,
  combat aiming, and mouse selection in supported menus.
- Optional smooth camera and actor movement, with independent movement and sprite-animation speeds.
- Optional diagonal movement and interactions, including combat and corner sliding.
- Transparent character and selected static-object sprites, inferred ground beneath
  objects, and foreground occlusion by southern blocking scenery.
- Optional Amiga, Apple II, Grayscale, and Sharp X68000 map/character tilesets.
- In-game Engine Options with persistent settings and the original font and blue pixel-art frame.
- Unlimited named save slots, gameplay thumbnails, play time, location labels,
  scrollable save/load browsers, and confirmed deletion.
- External music playback and optional WAV effects, with synthesized original DOS
  PC-speaker effects when recordings are absent.
- First-launch game/music directory selection, separate writable saves, and
  case-insensitive game-file lookup.

### Alternate tilesets

Choose **Tileset** in Engine Options to change overhead map and character artwork.
The selection is saved in `ENGINE.CFG`. Game rules, tile IDs, map dimensions,
mouse targeting, UI font, title screens, and cutscenes retain the DOS engine's behavior.
The renderer preserves each sheet's colors; Sharp's 32×32 art is drawn inside
16×16 logical cells, retaining its extra detail during smooth movement. The
existing display aspect ratio applies to all tilesets. CRT and darkness options
can be used with them.

The optional sheets live in `textures/tilesets` in this repository. For an installed
release, place a `textures/tilesets` folder beside `Impera.exe` or the Linux
AppImage; on macOS put it under `Impera.app/Contents/Resources`. Keep these names:

| Choice | Required sheets |
| --- | --- |
| Amiga | `Ultima_5_Tiles_Amiga.png` |
| Apple II | `Ultima_5_Tiles_AppleII.png` |
| Grayscale | `Ultima_5_Tiles_Grayscale.png` |
| Sharp X68000 | `Ultima_5_Tiles_SharpX68000_World.png` and `Ultima_5_Tiles_SharpX68000_NPC.png` |

Alternatively, set `IMPERA_TILESETS` to the folder containing the sheets. The
Linux development launcher finds the repository's sheets automatically.
These original-game artwork sheets are **not bundled into engine release packages**.
Missing or invalid sheets leave the current tileset active and show a message
in Engine Options; a saved choice whose sheets are unavailable falls back to DOS
at startup.

This is an artwork replacement, not emulation of the other platforms. Apple II
omits four columns of each atlas row (64 tile IDs); those IDs use DOS artwork.
Amiga and Grayscale contain all 512 tile positions. Sharp's two sheets also
cover all 512 IDs, but actor/effect slot `0x1F8` is blank; the DOS version has
artwork there. The blank world tile `0xFF` is intentional in all complete sheets.

Sharp uses its own **static artwork** for procedural effects, including water,
lava, moongates, clocks, flickering fire, and flags. These bitmaps are neither
animated nor replaced with DOS artwork. Authored character/object animation
frames still follow the game's frame selection. For Amiga, Apple II, and
Grayscale, effects constructed by modifying DOS palette bits retain DOS artwork;
water/lava scrolling and authored frame animations remain animated.
First-person dungeon scenery and other non-tile graphics remain DOS.

## Keyboard commands

The original game commands remain available. These are the port's main additions
and the keys used by its new screens:

| Key | Context | Action |
| --- | --- | --- |
| `Ctrl+O` | Gameplay | Open **Engine Options**; return to the suspended game afterward. |
| `Ctrl+W` | World, town, or dungeon command prompt | Open **Save Game**. |
| `Ctrl+L` | World, town, or dungeon command prompt | Open **Load Game**. |
| `Q`, then `Y` | Normal save command | Open **Save Game** instead of writing one fixed slot. |
| `O` | Main menu | Open **Engine Options**. |
| Arrow keys | Engine Options and save/load browsers | Move between entries; Left/Right adjusts speed settings. |
| `Enter` | New menus | Select an entry, toggle an option, or confirm a save name. |
| `Page Up` / `Page Down` | Save/load browsers | Scroll through slots. |
| `Delete` | Named save selected in a save/load browser | Ask for confirmation, then delete the slot with `Y`; `N` or Escape cancels. |
| `Escape` | Engine Options or save/load browser | Return to the menu or game. |
| `Escape` / `Space` | Action direction prompt | Cancel the action. |
| Two perpendicular arrow keys, or numpad `7/9/1/3`, or `Home`, `Page Up`, `End`, `Page Down` | Gameplay, with Diagonal Movement enabled | Northwest, northeast, southwest, southeast respectively. |
| `Ctrl+E` | Gameplay | Preserve the original **Exit to DOS?** confirmation. |
| `Ctrl+E` | Main menu, Engine Options, save/load browsers, or cutscenes | Exit immediately. |

`Ctrl+W` and `Ctrl+L` are gameplay shortcuts and do not open another browser from
inside menus or other prompts. Normal keyboard action selection still works when
mouse controls are enabled.

## Getting started

Download packaged builds from [Impera Releases](https://github.com/drcarademono/impera/releases).
To work from source, clone the repository:

```sh
git clone https://github.com/drcarademono/impera.git
cd impera
```

### Build and launch on Linux

Install a C compiler, CMake 3.16 or newer, Python 3, pkg-config, SDL3, and SDL3_mixer
development packages. This target uses **SDL3_mixer 3.2 or newer**, not
SDL2_mixer. SDL3 must include your desktop's X11 or Wayland video driver.
If your distribution does not package these libraries, build them from the
official [SDL](https://github.com/libsdl-org/SDL) and
[SDL_mixer](https://github.com/libsdl-org/SDL_mixer) releases. The cloud build
was tested with SDL 3.4.18 and SDL_mixer 3.2.4.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
bash scripts/run-linux.sh
```

For libraries installed under a custom prefix, add its `lib/pkgconfig` (or
`lib64/pkgconfig`) directory to `PKG_CONFIG_PATH` before configuring.

### Select your game files

On first launch, a pixel-art setup screen asks for the directory containing your
own Ultima 5 game files and a **Music (Optional)** directory. Select a row with
Up/Down and Enter, or click it, to open the system folder picker. **Start Game**
validates the game files and remembers both locations in `build/runtime/DATA.CFG`.
The setup screen returns if a selected directory or required game asset goes
missing. Delete `DATA.CFG` to choose different locations. **Clear Music** removes
the optional selection. Escape cancels startup.

Before the game files are available, setup uses the engine's public-domain 8×8
font. After choosing a valid game directory it uses the original `IBM.CH` font.
Game assets and music stay external; the engine does not embed or copy them.
Writable saves stay in `build/runtime/SAVEGAME`, leaving the selected installation
unchanged. `U5D_DATA_DIR=/absolute/path/to/game` also supports an explicit game
location for unattended launches. The separate music folder takes precedence
over a `Music` folder alongside the game files.

## Engine Options

Choose **Engine Options** below **Return to the View** in the main menu
(or press `O`). Press `Ctrl+O` during gameplay to open Engine Options
and return to your game with `Esc`. Video Mode, Mouse Control, Smooth Movement, Diagonal Movement,
Transparent Sprites, Music, and Sound Effects can be switched on or off.
Changes apply immediately. Movement Speed and Animation Speed have sliders
with ticks at 0.5, 0.75, and 1; use Left/Right or click and drag the slider.
Up/Down selects a setting, Enter toggles it, and Escape returns to the menu or your game.
The screen uses the original game font and integer-scaled pixels.

Settings are saved in `ENGINE.CFG` in the runtime directory and loaded on the
next launch. Explicit command line flags override saved settings. Sliders also
display command line speed values outside their tick range without changing
them until adjusted. Mouse input remains available on the settings screen
even when Mouse Control is off, so it can be turned back on.

### Available settings

| Setting | Effect |
| --- | --- |
| Video Mode | Dropdown: Windowed, Fullscreen 4:3 (vanilla 11×11 map, scaled to fit vertically with black side bars), or Fullscreen with the detected monitor aspect ratio (for example, 16:9). |
| CRT Filter | Subtle DOS monitor scanlines, phosphor softness, glow and nearly flat curvature. |
| Tileset | Dropdown: DOS (default), Amiga, Apple II, Grayscale, or Sharp X68000. Requires the corresponding external PNG sheets. |
| Transparent Sprites | Reveal ground around character and supported object sprites. |
| Dithered Darkness | Fade visibility edges with an Ultima 6-style ordered pixel pattern. |
| Music | Toggle playback. Switching on opens the folder browser only if no music folder is configured. |
| Sound Effects | Enable or mute WAV overrides and synthesized effects. |
| Mouse Control | Enable map movement and contextual actions; options menus remain clickable when off. |
| Smooth Movement | Animate camera scrolling and visible actor steps. |
| Diagonal Movement | Enable eight-way movement, neighboring actions, and combat. |
| Movement Speed | Adjust held movement and smooth tile transitions. |
| Animation Speed | Adjust all animated sprites independently of movement and turns. |

Selected rows use a white highlight with black text; checkbox colors remain
visible to show their state. Mouse hover selects rows, including **Return to
Menu** / **Return to Game**; left-click activates them. Sliders accept click
and drag. The menus use a pointer cursor rather than map direction arrows.

### Command-line-only options

`--legacy-save` enables **Legacy Save** in the load menu when valid original save
files exist. It is disabled by default and has no Engine Options setting.
`--help` lists the executable's launch options.

## Saving, loading, and starting a new game

Press `Ctrl+W`, or press `Q`, then answer **Y** to **Save game?** to open the **Save Game** browser.
Choose **New Save**, type a name (up to 26 characters in the game font), and
press Enter. Existing slots can be selected and overwritten after confirmation.
Names may repeat: each slot has its own identifier. Slots have no fixed limit;
the list shows four at a time with Up/Down, Page Up/Down, mouse wheel scrolling,
and a draggable scrollbar. Press Delete on a named slot, then Y to confirm deletion
or N/Escape to cancel. Escape or **Return to Game** cancels.

Completing **Create New Character** automatically creates a normal named slot
using the Avatar's name, fresh world state, and zero play time. The title-screen
flow stays the same: return to the title, then choose **Journey Onward** to find
the new game at the top of **Load Game**. Imported Ultima IV characters also get
a normal slot. Existing named saves are preserved.

**Transfer from Ultima IV** opens a file browser styled like the engine menus.
Navigate to your Ultima IV `party.sav` (filename case is ignored), then select it
with Enter or a left-click. Use Backspace to go up a directory, the arrow keys
or mouse wheel to scroll, and Escape to cancel. Missing, truncated, or invalid
saves show an error without exiting the engine.

Each slot includes the complete party save, both world-object lists, cumulative
play time, location metadata (map, level and coordinates), and a small screenshot
thumbnail for manual saves. Save and load rows show the location using the
existing map-name data. Older slots without location metadata remain loadable
and show **Unknown Location**. **Journey Onward** opens the matching
**Load Game** browser. `Ctrl+L` opens it during gameplay and resumes the selected
save immediately. Both `Ctrl+W` and `Ctrl+L` work at world, town, and dungeon
command prompts; they are inactive inside other menus. The command-line-only `--legacy-save` flag enables **Legacy Save** when valid original save files exist; it is hidden by default. This preserves access to the original
single-slot save. Slot data lives under `SAVEGAME/slots` in the runtime directory;
back up that directory along with `SAVEGAME` when moving your saves.

Play time measures elapsed time in loaded game sessions and persists with each
slot. Creating or importing a character starts a new clock. Existing DOS saves
have no historical play-time data, so their clock starts at zero. Menus count
as time within a loaded session; time while the application is closed does not.

## Mouse controls

Enable **Mouse Control** in Engine Options. The embedded cursors show the four or eight
movement directions over the overhead map and a pointer over menus, cutscenes,
and the status/text column. Cursor pixels scale with the displayed game pixels
using nearest-neighbor scaling, including when the window size changes. Hover
over an item in the main menu, party selector, equipment/scroll selector,
reagent list, shop inventory, or inn guest register to highlight it; left-click
to select it. Keyboard selection remains available.

All nine cursors are embedded in the engine executable, including the cursor
used by the CRT filter. No separate cursor files are required. Developers can
edit the source PNGs under `textures/cursors` and run
`python scripts/embed-cursors.py` to regenerate the embedded data.

Overhead map controls work in windowed and fullscreen mode:

- Hold the right button over the map to walk toward the cursor in eight
  directions when **Diagonal Movement** is enabled, or four otherwise. Release it to stop. Diagonal steps cannot cut through blocked
  corners; sailing retains the game's four-way headings.
- Single-left-click a tile at any distance to Look. Double-left-click an adjacent
  or (with **Diagonal Movement**) diagonal NPC to Talk, a door or chest to Open, or a loose object to Get. Other nearby
  targets fall back to Look. NPCs can also be double-clicked across one of the
  tables, desks, doors, or other tiles supported by keyboard Talk. Food on table
  settings can be taken from the same directions supported by keyboard Get.
- Double-click your own tile to Enter a town, Klimb a ladder, rest in a bed,
  or Board a vehicle when the usual keyboard conditions allow it.

After typing an action such as Talk, Search, Open, Get, Push, or Klimb, select
its direction with a single left-click on an eligible neighboring tile. Talk
also accepts NPCs across supported furniture. Keyboard directions remain available;
Escape or Space cancels the direction prompt.

Mouse actions work on the four or eight neighboring tiles, depending on the option.
Talk can reach an NPC two tiles away across supported furniture; other distant
targets do not trigger contextual actions. Look describes distant objects without
offering actions such as drinking or dropping coins; those require adjacency,
including a diagonal neighbor. Single clicks wait 300 ms to distinguish double
clicks. Right-button movement also works during combat command entry, relative to the
active fighter, including diagonals when enabled. Combat uses its usual turn and collision
rules. In combat, double-left-click an enemy within weapon range to start Attack
and position the Aim cursor on it. A separate single-left-click confirms the
attack during Aim; keyboard aiming and confirmation remain available. Attacks respect
the cardinal/diagonal option and the normal weapon, obstruction, and turn rules.
Map commands are disabled during menus, dialogues, dungeon
perspective views, and other input prompts. Menu lists support mouse selection;
use the keyboard for other prompts. Clicking the status column does not issue map commands.

Directional cursors target with their visible arrow tip, including after window
resizing. On combat maps, both cursor orientation and clicked action directions
are relative to the **active party character**, rather than the battlefield center.

## Movement and graphics

### Fullscreen maps

Choose the fullscreen entry labelled with your monitor’s detected aspect ratio
(for example, **Fullscreen 16:9**) from the **Video Mode** dropdown in Engine Options
to fill your display, or **Fullscreen 4:3** for a centered 4:3 map viewport. Menus and title screens also use 4:3 in this mode.
Choose **Windowed** to return to a window.
This detects the desktop resolution, adds overhead map rows and columns, and
moves the status and command column to the right edge. Tiles and text use the
historical DOS pixel proportions (pixels are 1.2 times taller than wide),
with integer horizontal scaling. Extra width adds map columns while sprites,
text, and the sidebar keep the proportions of the original 4:3 display. Daylight reveals the
expanded view; darkness and obstacles still restrict visibility. Combat retains
its original 11x11 battlefield. Title screens and dungeon perspective views keep
their original layout, centered without stretching. A few unused edge pixels
may remain when the display dimensions are not divisible by the pixel scale.
Without the option, the game uses the existing window size settings.
`--help` lists launch options.

### Diagonal movement

Enable **Diagonal Movement** in Engine Options to use eight directions for movement, neighboring
interactions, mouse cursors, and combat movement and attacks. This applies to
keyboard and mouse controls. Hold two perpendicular arrow keys together to move
diagonally, or use numpad `7/9/1/3` for northwest/northeast/southwest/southeast.
It also applies to AI-controlled combatants, including NPCs,
monsters, and party members. When one side of a diagonal is blocked, movement
slides along the open side for that turn. Continued input keeps trying the
original diagonal. If both sides are blocked, movement stops; diagonal steps
still cannot cut blocked corners.

Without this option, movement, actions, and combat attacks use cardinal
directions. The map has four mouse cursor zones (north, south, east, west).
Distant Look descriptions remain available in either mode, but Look actions
require a cardinal neighbor by default or any of the eight neighbors when
**Diagonal Movement** is enabled.

### Smooth movement and speed

Enable **Smooth Movement** in Engine Options. Each successful
tile step animates a scrolling camera over roughly 120 ms, keeping the player
icon centered and the interface fixed. NPCs and monsters also animate their
visible tile steps, including combat actors. Their motion stays coordinated
with camera scrolling; the combat camera stays fixed. This works with keyboard
or mouse movement; collisions and actions still use the original tile and turn
rules. Teleports, spawns, map changes, and dungeon perspective views snap to
their new state without interpolation.
Use **Movement Speed** to set the held mouse and keyboard movement speed,
and **Animation Speed** to set animated sprite playback independently,
including NPC poses, fountains, flames, and water effects.
Both default to `1`; the menu sliders offer `0.5`, `0.75`, and `1`.
A multiplier of `0.5` halves the speed. Sprite animation speed works with or without
**Smooth Movement** and does not change camera transition timing or game turns.
Movement speed also scales smooth tile transitions so held input continues
without waiting at tile boundaries.
Smooth movement uses timed keyboard repeats so held input continues across tile
boundaries; otherwise keyboard repeat retains the operating system's behavior.

### CRT filter

Enable **CRT Filter** in Engine Options or launch with `--crt-filter` for a
**DOS Monitor** preset, inspired by a good late-1980s MS-DOS color CRT:
subtle scanlines, slight horizontal phosphor softness, mild bloom and halation,
a gentle vignette and nearly flat curvature. Brightness compensation keeps
the image close to the original. A very subtle RGB grille is averaged in small
windows to avoid colour aliasing; there is no chromatic
aberration, noise or flicker. The filter uses lightweight SDL render passes
with the same effect on OpenGL and software renderers. Original pixels are
scaled before CRT processing, using the existing integer scaling in fullscreen
and menu layouts. It applies to
gameplay, menus, cutscenes and the mouse cursor, including smooth movement
and fullscreen. Save thumbnails retain the clean gameplay image.
It is off by default and saved in `ENGINE.CFG`. At small window sizes the
scanlines are averaged to keep text readable.


### Dithered darkness

Enable **Dithered Darkness** in Engine Options for an Ultima 6-style pixel-pattern
fade along night-time and line-of-sight boundaries. It is off by default. The
player's tile and visible masonry (including stone walls, windows, crenellations,
doors and fireplaces) stay
clear, masonry stops the fade from reaching onto the ground inside walls,
fully unseen terrain stays black, and gameplay
visibility rules are unchanged. The pattern uses original game pixels and stays
attached to the map during smooth scrolling. It works in windowed and expanded fullscreen
overhead views; combat and dungeon perspective views keep their usual rendering.

The matching launch flag is `--dithered-darkness`. Like other Engine Options,
the setting is saved in `ENGINE.CFG`; the flag enables it for startup and overrides
the saved value.

### Transparent sprites

Enable **Transparent Sprites** to draw player, NPC, and monster sprites over their ground
instead of replacing the entire tile. Black sprite pixels are transparent,
except for a one-pixel black outline around colored pixels (including diagonal
neighbors). The outline can extend one pixel into adjacent map tiles and is
clipped at the map frame. Blocking scenery to the south is drawn in front
of overlapping sprite outlines. This also applies to expanded fullscreen maps and
smooth movement. Fountains (all animation frames), wells, braziers, cannonballs, cannons,
telescopes, stacks of logs, stocks (empty or occupied), guillotines,
torture racks, pendulums (all animation frames), metal grates, portcullises,
carpets, ladders, and bellows also use this setting. Their map cells
have no separate ground layer, so their background uses the most common
immediately adjacent brick floor, grass, stone floor, or wooden floor tile
(ties prefer that order). With no adjacent recognized ground, they stay opaque. This
inference affects rendering only and leaves map data and interactions unchanged.
The static-object audit checked all 256 map tiles against the tile art, object
names, and town/castle map usage. It includes only freestanding objects with
black backgrounds around their artwork. Furniture such as barrels, beds, drawers, chairs,
tables, and street lamps already includes colored floor or grass pixels;
these keep their original artwork. Solid walls, doors, stairs, fences, other wall fixtures,
terrain, and effects also retain their original rendering.
Chairs, tables (including food and candelabrum variants), and all seated NPC
poses stay opaque with no added outline.
The sleeping-in-bed NPC, empty manacles, and all four occupied-manacles NPC
frames retain their complete opaque artwork with no added outline, even with
**Transparent Sprites**.
Sprites default to opaque unless Transparent Sprites is enabled. Terrain tiles remain opaque;
the original tile data is unchanged.

## Music and sound effects

Download the **Ultima 5 MP3 music** from [Exodus — Downloads](https://exodus.voyd.net/downloads/) and extract it before selecting its folder.

Player-supplied music belongs in `Music` inside the data directory, or beside
the executable (for example `build/Music` when running the Linux build).
The same locations work for `Sound` overrides. Music files are
selected by their two-digit track prefix, so names such as
`02 - Britannic Lands.mp3` work without renaming. MP3, Ogg, WAV, and FLAC are
supported. Slots 01–15 use the game's existing music assignments; 16 (Amiga
Theme) is accepted but has no automatic gameplay assignment. Music stays
external and is not bundled into the executable. Legacy `BGM/01.ogg` through
`BGM/15.ogg` remain supported as fallbacks.

Optional WAV sound overrides belong in `Sound`, using the filenames listed in
`src/audio/sfx_map.h` (for example `step0.wav`, `step1.wav`, and `fountain.wav`).
Each missing or unreadable effect falls back to the original PC-speaker tone,
noise, pulse, or sweep algorithm. Frequencies are Hz and become integer PIT
divisors via `1193182 / frequency`. Noise uses the original 16-bit PRNG;
pulses gate the divisor-60 carrier using wrapping 16-bit accumulators and
thresholds. Sweeps retain DOS signed 16-bit arithmetic and integer division.
`sfx_map.h` remains the authoritative effect parameter table. Title effects
without parameter tuples require their WAV recordings.

SDL converts each calibrated delay unit to **50 microseconds**, consistently
across all effects: tone duration is `dur` units, noise holds each frequency
for `rate` units until elapsed reaches `dur`, PWM performs `dur` iterations
of `delay` units, and sweeps hold each step for `tickStep` units. This is an
explicit timing conversion, not an emulation of DOS CPU calibration via
`D_5356`/`D_5426`; it does not alter the sound algorithms. PCM is sampled at
48 kHz without frequency/duty clamps or click envelopes. Legacy `SFX` files
also remain supported.
Audio is read directly from the optional selected music folder or the usual
external audio directories; players can also place it in `build/runtime`.
No audio assets are needed for synthesized effects. Do not set SDL's dummy video/audio drivers when playing on your desktop.

## File locations and compatibility

The launcher keeps writable saves in `build/runtime/SAVEGAME` and reads game
assets from the directory selected in setup. Existing saves are preserved. Set `U5D_DATA_DIR` to an absolute
path to use another data directory, or `U5D_BUILD_DIR` to an absolute path
if you configured a different build directory. Game filenames and directory
components are matched without regard to ASCII letter case, including fonts,
maps, NPCs, conversations, saves, Ultima IV imports, and optional audio.
Existing saves retain their filename spelling when written. Exact spelling
wins; if a lookup has multiple matches differing only by case, it reports an
error rather than choosing one.
New character creation requires the original `INIT.GAM` file (at least 4192
bytes), which supplies the starting party and world state. Add it to the data
directory if it is missing; `INIT.OOL` is a different file and cannot replace
it. Choose **Create New Character** before **Journey Onward** when there is
no active saved character.

Characters previously created without `INIT.GAM` have invalid starting state.
Back up `build/runtime/SAVEGAME`, restore the original `INIT.GAM`, and create
a new character. Rebuilding alone cannot repair an already damaged save.

## Troubleshooting

Runtime logging is **always enabled**, starting before the first-launch directory
picker. No environment flag is needed. With the Linux launcher, logs live in
`build/runtime/LOG.TXT` and `build/runtime/LOG.PREV.TXT`; direct launches write
them in the working directory. Errors also go to the terminal. If the log cannot
be created, error messages still go to the terminal.

Logs include timestamps and sequence numbers, build/SDL/platform information,
selected data/audio directories, renderer and audio setup, settings changes,
map/position/active-combatant changes, gameplay commands, file transfers, and
save/load/delete outcomes. Records are flushed immediately. Each log segment is
limited to approximately 4 MiB; the previous segment or previous launch is kept
as `LOG.PREV.TXT`. Copy **both files before restarting repeatedly**, and include
the steps to reproduce, operating system, and affected save when reporting a bug.
Logs can contain local directory paths and gameplay state.

Linux/macOS fatal signals append a crash marker and retain normal signal/core-dump
termination. Windows retains its existing crash dump handler. These diagnostics
preserve the last recorded context; they do not guarantee a stack trace or explain
every crash. A normal exit records **Clean application shutdown**. `U5D_DEBUG=1`
is accepted as an environment variable but is no longer needed to enable tracing.

Missing required map or NPC data now exits with an error naming the file
instead of retrying indefinitely.

## Development

To run the native settings and savegame regression checks:

```sh
cmake -S . -B build -DU5D_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

### Automated executable releases

GitHub Actions builds Windows x64, Linux x64, macOS Intel, and macOS Apple
Silicon packages using [Build releases](.github/workflows/release.yml).
The release build pins SDL 3.4.18 and SDL_mixer 3.2.4, links them statically,
and includes built-in WAV, MP3 and Ogg Vorbis decoding. Linux releases provide
only an AppImage (plus its checksum). Original Ultima 5 game files,
music, saves and personal settings are never included in the packages.
ScaleFX is not part of these releases.

To test a build, open **Actions → Build releases → Run workflow**, select the
branch, and download its platform artifacts when all jobs finish. GitHub wraps
Actions downloads in a ZIP; the Windows artifact contains the executable
directly. Release assets have no extra wrapper: one Windows ZIP, one Linux
AppImage, and one macOS archive per architecture, with checksums. To make a
release after merging, tag the desired commit:

```sh
git switch main
git pull --ff-only
git tag v0.1.0
git push origin v0.1.0
```

The workflow runs tests and attaches archives and SHA-256 checksums to a
GitHub Release. If none exists, it creates a **draft**; otherwise it reuses the
existing release, whether draft or published. Reruns replace matching assets
and their checksums. Download and test each platform before publishing a new
draft from GitHub's Releases page. A failed platform build prevents release
creation. Release jobs use GitHub's built-in token; no personal token is needed.
Manual workflow runs only upload artifacts and do not create a release.

Tag runs use the workflow and source code recorded in that tag. Rerunning an
older tag does not pick up later workflow fixes; use a new tag for a new build,
or upload recovered packages to the existing release with
`gh release upload TAG FILES... --clobber`.

Launch the download for your platform:

- **Windows:** run `Run Impera.cmd` from the extracted folder. Saves and logs stay in this folder; cursor assets are embedded.
- **macOS:** extract the archive, move `Impera.app` to Applications, and open it.
  Requires macOS 12 or newer; choose the Intel or Apple Silicon download. Settings, saves and
  logs live in `~/Library/Application Support/Impera`.
- **Linux:** download the `.AppImage`, make it executable with
  `chmod +x Impera-*.AppImage`, then double-click it or run `./Impera-*.AppImage`.
  If FUSE is unavailable, run `./Impera-*.AppImage --appimage-extract-and-run`
  instead.
  Settings, saves and logs live in `$XDG_DATA_HOME/impera`, or `~/.local/share/impera` by default.
  Linux builds target Ubuntu 22.04 or newer compatible systems. The AppImage
  bundles the engine, SDL, its audio decoders and cursor assets; system graphics
  drivers and audio/display services still come from the host.

On first launch, select your own Ultima 5 directory and optionally a music
folder. Builds are unsigned: macOS may require allowing the app in Privacy &
Security, and Windows may show a SmartScreen prompt. Signing/notarization is
not configured in this workflow.

To build the same engine locally, install CMake, a C compiler and platform SDL
development prerequisites, then run:

```sh
cmake -S . -B release-build -DCMAKE_BUILD_TYPE=Release -DIMPERA_BUNDLED_SDL=ON
cmake --build release-build --config Release --parallel 3
python scripts/package-release.py --build release-build --platform linux-x86_64 --version v0.1.0
```

Use `windows-x86_64`, `macos-x86_64`, or `macos-arm64` when building natively on
those platforms. CMake downloads pinned dependencies on the first build.
Packaging requires Python 3.9 or newer and writes archives to `dist/`.
To produce the Linux AppImage from that archive:

```sh
python scripts/build-appimage.py dist/Impera-v0.1.0-linux-x86_64.tar.gz
```

The script downloads appimagetool 1.9.1 and the type-2 runtime dated 20251108,
verifies their pinned SHA-256 hashes, and caches them under
`release-build/appimage-tools/`. AppImage packaging and CI smoke checks do not
require FUSE. The AppImage contains no Ultima 5 game files. Its read-only image
uses the same external save/settings directory as the portable Linux launcher.

### Windows Target

Open `u5win/u5win.slnx` with Visual Studio 2026 and build the solution.

### MS-DOS Target (DJGPP)

Run `make` on a machine with DJGPP installed.

Depending on the environment, you may need to adjust the `CROSS_CC_PREFIX` value in the `Makefile`.

### MS-DOS Target (Original Toolchain)

Run `src\build.bat` on a machine or virtual machine with Microsoft C 5.1 installed.

The compiler installation path is `C:\MSC51`.

Note: executable linking is currently not possible. This target is used only for disassembly matching.

## Upstream project

The upstream [u5d project](https://github.com/wonst719/u5d) reconstructs Ultima V
v1.16 for MS-DOS, aiming for functional equivalence and assembly-level semantic
matching. Its original linker has not been found, so perfect binary matching is
not currently possible. FM-TOWNS disassembly is also used as a reference where
needed. See upstream for current decompilation progress and its original
project documentation.

## Credits

- **[carademono](https://github.com/drcarademono)** — Impera port and enhancements.
- **[wonst719](https://github.com/wonst719)** — [u5d — Ultima V Decompilation Project](https://github.com/wonst719/u5d), the base engine and original decompilation.
