#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${U5D_BUILD_DIR:-$repo_dir/build}"
data_dir="${U5D_DATA_DIR:-$repo_dir/Ultima 5}"
runtime_dir="$build_dir/runtime"

if [[ ! -x "$build_dir/ultima5" ]]; then
    echo "Build the game first; see the Linux instructions in README.md." >&2
    exit 1
fi
if [[ ! -f "$data_dir/TITLE.BIT" || ! -f "$data_dir/IBM.CH" || ! -f "$data_dir/RUNES.CH" ]]; then
    echo "Original game data is missing from $data_dir (TITLE.BIT, IBM.CH, RUNES.CH)." >&2
    exit 1
fi
if [[ ! -f "$data_dir/INIT.GAM" ]] || [[ $(wc -c < "$data_dir/INIT.GAM") -lt 4192 ]]; then
    echo "Missing or incomplete $data_dir/INIT.GAM (requires at least 4192 bytes)." >&2
    echo "Restore INIT.GAM from the original game installation before creating a character." >&2
    exit 1
fi

mkdir -p "$runtime_dir/SAVEGAME"
for source in "$data_dir"/*; do
    [[ -f "$source" ]] || continue
    case "${source##*.}" in
        CH|HCS|BIT|PTH|DAT|16|4|OOL|CBT|NPC|TLK|PCS|GAM)
            target="$runtime_dir/${source##*/}"
            if [[ ! -e "$target" ]]; then cp -- "$source" "$target"; fi
            ;;
    esac
done
for name in BRIT.OOL UNDER.OOL SAVED.OOL SAVED.GAM; do
    if [[ -f "$data_dir/$name" && ! -e "$runtime_dir/SAVEGAME/$name" ]]; then
        cp -- "$data_dir/$name" "$runtime_dir/SAVEGAME/$name"
    fi
done
for name in BGM SFX; do
    if [[ -d "$data_dir/$name" && ! -e "$runtime_dir/$name" ]]; then
        cp -R -- "$data_dir/$name" "$runtime_dir/$name"
    fi
done

cd -- "$runtime_dir"
exec "$build_dir/ultima5" "$@"
