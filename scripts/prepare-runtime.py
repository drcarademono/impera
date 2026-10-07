"""Prepare writable game data without depending on filename or directory case."""
import os
from pathlib import Path
import shutil
import sys


def resolve(path, allow_missing=False):
    path = Path(path).absolute()
    current = Path(path.anchor)
    parts = path.parts[1:]
    for index, component in enumerate(parts):
        exact = current / component
        if os.path.lexists(exact):
            current = exact
            continue
        matches = [p for p in current.iterdir() if p.name.lower() == component.lower()]
        if len(matches) > 1:
            raise ValueError(f"Ambiguous filename: {exact}")
        if matches:
            current = matches[0]
        elif allow_missing and index == len(parts) - 1:
            current = exact
        else:
            raise FileNotFoundError(f"Missing game data: {exact}")
    return current


def prepare(data_dir, runtime_dir):
    data = resolve(data_dir)
    for name in ("TITLE.BIT", "IBM.CH", "RUNES.CH", "INIT.GAM"):
        asset = resolve(data / name)
        if not asset.is_file():
            raise ValueError(f"Not a game data file: {asset}")
        if name == "INIT.GAM" and asset.stat().st_size < 4192:
            raise ValueError(f"Incomplete {asset}: requires at least 4192 bytes")

    # Reject conflicting source spellings rather than copying an arbitrary one.
    entries = sorted(data.iterdir())
    seen = set()
    for entry in entries:
        folded = entry.name.lower()
        if folded in seen:
            raise ValueError(f"Conflicting filename spellings in {data}: {entry.name}")
        seen.add(folded)

    runtime = resolve(runtime_dir, allow_missing=True)
    runtime.mkdir(exist_ok=True)
    saves = resolve(runtime / "SAVEGAME", allow_missing=True)
    saves.mkdir(exist_ok=True)
    extensions = {".ch", ".hcs", ".bit", ".pth", ".dat", ".16", ".4", ".ool",
                  ".cbt", ".npc", ".tlk", ".pcs", ".gam"}
    for source in entries:
        if source.is_file() and source.suffix.lower() in extensions:
            target = resolve(runtime / source.name, allow_missing=True)
            if not os.path.lexists(target):
                shutil.copy2(source, target)
    for name in ("BRIT.OOL", "UNDER.OOL", "SAVED.OOL", "SAVED.GAM"):
        try:
            source = resolve(data / name)
        except FileNotFoundError:
            continue
        target = resolve(saves / name, allow_missing=True)
        if not os.path.lexists(target):
            shutil.copy2(source, target)
    for name in ("Music", "Sound", "BGM", "SFX", "U4SAVE"):
        try:
            source = resolve(data / name)
        except FileNotFoundError:
            continue
        target = resolve(runtime / name, allow_missing=True)
        if source.is_dir():
            if name != "U4SAVE":
                shutil.copytree(source, target, dirs_exist_ok=True)
            elif not os.path.lexists(target):
                shutil.copytree(source, target)
    return runtime


if __name__ == "__main__":
    try:
        print(prepare(sys.argv[1], sys.argv[2]))
    except (OSError, ValueError) as error:
        sys.exit(str(error))
