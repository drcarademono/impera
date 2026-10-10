# Impera Workshop — native mod authoring (first version)

Impera Workshop is a C++17 / Qt 6 desktop application, not a Python wrapper.
It reads original DOS Ultima 5 resources, keeps edits in a project, and exports
**`.imperamod` packages**. Neither editing nor exporting rewrites the game files.
The app is an initial implementation; it is not yet a finished replacement for
all of the specialized editor interfaces.

## Build and run

Install a C++ compiler, CMake 3.16 or newer, and Qt 6.2+ Widgets development tools.
On Ubuntu/Debian, the additional development package is `qt6-base-dev`.
On Windows/macOS, use the Qt installer and set `CMAKE_PREFIX_PATH` to its kit.

Build Workshop independently (SDL and Python are not required):

```sh
cmake -S workshop -B workshop-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build workshop-build --config Release --parallel 3
ctest --test-dir workshop-build -C Release --output-on-failure
```

The executable is `workshop-build/ImperaWorkshop` on Linux,
`workshop-build/Release/ImperaWorkshop.exe` on Windows, or
`workshop-build/ImperaWorkshop.app` on macOS. Qt runtime libraries are required;
these development builds do not yet have a portable release deployment pipeline.
Use `windeployqt` / `macdeployqt` when distributing a development build. The
Windows resource, macOS bundle and embedded window icon use Impera's artwork.

Alternatively, add `-DIMPERA_BUILD_WORKSHOP=ON` to the engine's CMake configure
command. Normal engine builds do **not** need Qt. CI builds and tests the native
application on Linux x86_64, Windows x86_64 and both macOS architectures.

```sh
workshop-build/ImperaWorkshop --game '/path/to/Ultima 5'
workshop-build/ImperaWorkshop --project my-mod.imperaproject
```

## Authoring workflow

1. **New Mod**: choose the original DOS game directory. Workshop does not load
   installed mods into the base game; you can explicitly import a package.
2. **Name Mod**: give the project a name used in Impera's diagnostics.
3. Choose a resource from the searchable library and edit it.
4. **Save Project** creates `.imperaproject` JSON containing the original folder
   reference/checksums and changed resources. Keep this project for future editing;
   it requires the same original files when reopened. It is not a distribution file.
5. **Export Package** creates a sparse, validated `.imperamod` file.
6. Copy the package into **`<selected Ultima 5 folder>/Mods/`**, then restart Impera.
   Create `Mods` if it does not exist. To disable a mod, move its package out and
   restart. There is no in-game mod-selection UI yet.

Keyboard: **Ctrl+N** new project, **Ctrl+O** open project, **Ctrl+S** save,
**Ctrl+Z** undo, **Ctrl+Shift+Z** redo, **Ctrl+E** export. Qt uses the usual Command
shortcuts on macOS for standard editing/project actions. Dialogue and story edits
have an explicit **Apply** button; navigation, saving and export check unapplied
text rather than silently discarding it. Package imports become undoable edits.

## Workspaces

| Workspace | Available in this first version |
| --- | --- |
| Maps | Paint Britannia, Underworld, all settlement floors and combat maps; tile palette, zoom, grid, right-click eyedropper, per-stroke undo; PNG render and tile-ID exports |
| Settlement NPCs | Schedule overlays for all four time slots; click in Inspect NPCs mode to open the appropriate NPC; edit three AI/X/Y/Z locations, four hours, appearance and dialogue ID |
| Combat setup | Edit starting party positions for four directions, monster tiles/positions, triggers and changed-tile metadata; retain all padding/sentinel bytes |
| Graphics | All DOS `.16` image containers and 512 tiles; thumbnail gallery, EGA pixel painting, PNG import/export, full tilesheet import/export, one-bit alpha masks |
| Conversations | NPC selector, lossless word/control tags, all fifteen labels, explicit command operands, Apply, missing-tail repair; enforce header/offset/buffer limits |
| Story | Twenty fixed-offset text pages; original capacity, offsets, padding and terminators retained |
| Starting state | Party position, time, supplies, plot items, reagents, moonstones, shrine/dungeon flags, Shadowlord settings; sixteen character records, statistics/equipment, safe party joining/removal |
| Resource inspector | Paged hex editing, per-edit undo, importing outputs from the Python editors, reverting a resource; other resource types remain available here |

Basements use the engine's actual floor numbers. Britannia changes rebuild both
`BRIT.DAT` and the chunk-index bytes of `DATA.OVL` together; implicit-water and
shared chunks are handled. More than 255 distinct non-water chunks is rejected.
Underworld and settlement extension bytes are retained.

Artwork must use the exact DOS EGA palette. Alpha is binary; `TILES.16` has no
alpha channel. Alternative-platform/custom PNG tilesets in Engine Options are
independent of the DOS graphics resources edited here.

Conversation text uses `<Entry>` for NUL separators, `<New Line>` for in-game
line breaks, `<the>` and other exact word tags for compressed tokens, and
`<Byte N>` for explicit bytes. Editor newlines are formatting. `<Gold>` needs
three byte operands, `<Change>` and `<Set Flag>` one, and `<Byte 254>` two.
Label definitions need `<Any><Label N>`; jumps use `<Label N>` without `<Any>`.
These expose actual DOS semantics; they are not a new scripting language.

Starting-state mods primarily affect **new games**. The original character-
creation sequence still overwrites the fields it traditionally initialized.
Existing saves retain saved state, object lists and NPC data. Initial world
object overrides seed a fresh runtime save directory; they do not overwrite an
existing player's object files. Removing a mod does not undo changes already
captured in a saved game.

## Scope and next work

This branch establishes native editing and end-to-end package loading. It covers
the audited formats and supports bringing existing editor outputs into packages.
The dialogue tree/branch visualization and interactive placement tools for combat
entities are still to come; corresponding data is currently edited in tagged text
and tables. There is no flood fill, multi-tile selection, plugin scripting,
package dependency system, runtime hot reload, mod manager or portable Workshop
release packaging yet. Unknown resources and fields use the byte inspector.
Windows/macOS CI is configured; local interactive validation was on Linux only.

## Validation

Native tests cover compression width/reset boundaries **against the actual C
engine decoder**, paired graphics offsets/masks, PNG alpha/palette rejection,
dialogue token/control roundtrips, story offsets, map chunk allocation,
projects/packages/checksums and widget loading without mutation. Set
`U5_GAME_DIR` to your original game directory to include optional full-resource
and all-workspace checks. No game assets are included in generated test fixtures.

```sh
QT_QPA_PLATFORM=offscreen U5_GAME_DIR='/path/to/Ultima 5' \
  ctest --test-dir workshop-build --output-on-failure
```

The engine also has a `mod_packages` CTest covering corrupt/truncated packages,
all-or-nothing mounting, load order, conflicts, reloads and read/write isolation.
See [the package format](../docs/mod-packages.md) for the shared engine contract.
