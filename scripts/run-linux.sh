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
runtime_dir="$(python3 "$repo_dir/scripts/prepare-runtime.py" "$data_dir" "$runtime_dir")"

cd -- "$runtime_dir"
exec "$build_dir/ultima5" "$@"
