# Map JSON transfer

Choose **Export → Map JSON…** in a map editor, then use **Import…** beside Export
in the destination project's map editor. Import replaces the selected map in one
undoable edit. Exporting a location or dungeon includes all of its floors.

Files carry tile grids and dimensions, NPC schedules and conversations, signs,
objects, container contents and properties, encounter placements and triggers,
and dungeon encounter rooms. World files include party-start and moonstone
positions and entrance links. Authored destinations reached by those entrances
are included; vanilla destinations resolve to their base-game map identities.
Other destinations already present in the project are retained.

Custom type definitions and artwork travel with the map. Conflicting custom IDs
and conversation numbers are remapped; identical custom types are reused. Used
vanilla artwork and type overrides update their shared slots. Artwork includes
DOS and alternate tilesets, with binary data stored as Base64 strings in JSON.
Game-wide rules, shop tables, cutscenes, and editor-only question annotations are
separate project data.

Import requires a matching map category: world, location, dungeon, or combat.
Outdoor and dungeon combat maps retain their distinct encounter formats. New
custom locations adopt the incoming floor layout, including basements and upper
floors. Built-in locations require a matching number of floors. Dimensions may
change, subject to existing map-position validation; invalid or incomplete files
leave the destination untouched. Files are limited to 128 MiB.

## Format

The root identifies `format: "impera-map"` and `version: 1`. `map` contains its
`kind`, `name`, and `floors`; every floor has a numeric floor index, width, height,
and a rectangular `tiles` array. Dungeon tile values are native feature bytes;
other grids contain vanilla or custom tile IDs. Locations carry `npcs` and a
shared `dialogues` dictionary. Dungeons carry sixteen nested combat `rooms`.
World `entrances` describe vanilla references or embedded authored destinations.

`customTypes` and `vanillaTypes` hold referenced type records. `artwork` contains
DOS artwork and alternate PNG sheets with a mask of used slots. Import stages all
resources privately, validates the result, and commits the complete change once.
