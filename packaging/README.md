`impera.svg` is the canonical Impera application icon. The PNG, ICO, ICNS and
embedded window pixels are generated derivatives, not separate artwork.

To regenerate after changing the SVG:

```sh
python -m pip install CairoSVG Pillow
python scripts/generate-icons.py
python scripts/generate-icons.py --check
```

CairoSVG requires Cairo on the generation machine. Ordinary builds and the
`--check` command do not need CairoSVG or Pillow; the generated files are checked
in. The manifest records hashes of both the SVG and its derivatives.

Windows embeds the ICO using `impera.rc`; macOS bundles the ICNS and names it in
`Info.plist`; AppImage uses the SVG for its desktop entry and `.DirIcon`.
SDL windows use embedded RGBA pixels from the same SVG, so the startup picker
and game window need no external icon files.
