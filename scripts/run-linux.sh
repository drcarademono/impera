#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${U5D_BUILD_DIR:-$repo_dir/build}"
runtime_dir="$build_dir/runtime"

if [[ ! -x "$build_dir/ultima5" ]]; then
    echo "Build the game first; see the Linux instructions in README.md." >&2
    exit 1
fi
if [[ -n "${U5D_DATA_DIR:-}" ]]; then
    export U5D_DATA_DIR="$(realpath -- "$U5D_DATA_DIR")"
fi
export IMPERA_TILESETS="${IMPERA_TILESETS:-$repo_dir/textures/tilesets}"
mkdir -p "$runtime_dir"

cd -- "$runtime_dir"
exec "$build_dir/ultima5" "$@"
