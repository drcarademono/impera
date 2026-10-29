# Ultima IX city-design work

Work only on temporary branches based on `ultima-ix`; target every pull request
to `ultima-ix`, never `main`. The permanent `ultima-ix` branch is the game-content
track. Do not merge tracks or push content into `main`. Use the existing isolated
checkout; create a worktree only if the user explicitly requests one.

Read `README.md`, the city's design report, `docs/map-json.md`,
`docs/workshop-tile-catalog-audit.md`, and the relevant engine rules before editing.
Use `python -m tools.city_design` from the repository root. Install Pillow from
`requirements.txt` if needed; the format, editor and routing code is standard Python.

An actual current Workshop export is mandatory. Do not reconstruct a missing
export from vanilla banks or invent missing NPC data. Record the source file's
SHA-256 and commit. Never overwrite an original, an earlier experiment, or a PNG
being used as visual evidence. Write each experiment into a new directory.

For Minoc, the original export remains immutable. Preserve all original terrain
except the **explicitly authorized perimeter/gateway cells** recorded by
`authorized_wall_cells` and `study/validation.json` (61 allowed cells on each floor).
The user later authorized wrapping an irregular wall around the expanded district
and moving the entrance to the bottom center. This supersedes the earlier blanket
terrain-preservation instruction only for those exact wall/gateway cells. Preserve
all NPCs, schedules, dialogue, signs, contents, building interiors and unrelated
metadata. Two new always-open gate records are the only authorized JSON metadata
replacement. Do not extend these exceptions to other terrain or metadata.

Start with `CityMap.load`, `protect_original`, resize both floors to 64×64, and edit
new cells. Explicit exceptions use `authorize_terrain` and `authorize_metadata` with
a reason, never blanket unprotection. A loaded revision uses the authoritative
source as `city.original`. `save` checks protection and exclusive output creation.

The current gate spans x31–32 at y61; the open passage and player arrival are at
`(32,62)`, with an exit approach on row63. The irregular perimeter and its approved
old-wall removals are in the recipe. Route the new approach east around the lodging,
not through it. Location JSON has no portal-arrival field: the companion
`Minoc-experiment.imperamod`, produced by the native probe, sets entry through the
existing map catalogue while retaining the world entrance `(159,20)`. Do not
mislabel `partyStart` (global new-game position) as a town-entry setting. Do not
modify the engine to add an unnecessary JSON field. Verify arrival survives a
package import into a second private project. Game play-testing is still required.

Design deliberately: establish a street hierarchy and public/service circulation,
then choose building footprints for their jobs. Use varied masses, corner cutouts,
work yards, loading approaches, sleeping bays and communal rooms rather than repeated
identical boxes. Furniture must leave useful aisles. Use verified catalogue IDs:
`0x58` is an anvil; `0x80` is a pendulum, not a forge; `0x27/28` are roofs;
`0xca/cb` are fences. Do not infer semantics from a tile's color alone. Native `0x20–26` road tiles are
directional narrow components: never repeat `0x20` as a dirt rectangle. Use
`paths.dirt_path` to match directions, or continuous paving for broad streets. Do not add
major NPCs, quests, factions or a story for the guild's collapse. Existing guild
terrain remains protected; this study does not authorize its demolition.

Render the source and **each** revision with the embedded DOS artwork. Inspect
full-map images and 2×/3× nearest-neighbor close-ups using image-viewing tools.
Record concrete problems, apply a revision, render again and inspect again.
Look for collision at entrances, blocked approaches, abrupt materials, repetitive
footprints, unused rooms, mismatched multi-cell furniture and severed roads.
Do not claim visual review from generating files alone. The renderer intentionally
rejects custom-art frames rather than substituting DOS images; use Workshop for
unsupported cases. Native previews are static terrain, not runtime animation,
lighting, actors or inferred sprite floors.

Run `python3 -m unittest discover -s tools/city_design/tests -v`. Regenerate the
catalogue with `build_catalogue` only after checking the audited sources; native
movement parity must still hold. Analyze designated entrances/interior work cells
from a known start, inspect components and hazards, and check floor connections.
Door-relaxed connectivity is conditional; actors, locks, schedules, gates and
runtime scripts require human/engine review. JSON validity alone is insufficient.

Use the optional `workshop_probe` and `verify_workshop` instructions in README
to call the real importer, validate the project, re-export, and compare native
pixels and movement bits. Record GUI interaction, native importer validation and
actual game play-testing separately. Never call a map play-tested unless it was
loaded and exercised in Impera. Stop for human review after the initial district;
do not redesign the rest of the city or merge the PR autonomously.
