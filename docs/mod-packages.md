# Impera mod packages, version 1

Impera automatically scans the **selected game directory's `Mods` subfolder**
at startup. Only files whose extension is `.imperamod` (case-insensitive) are
loaded, in deterministic bytewise filename order. There is no extraction or
execution of package content. Mounted resources are read-only startup snapshots;
read handles have the ordinary seek/read behavior expected by existing loaders.
Writable saves and engine settings never go through these overrides.

All entries are checked against the **unmodified original resources**, not a
previous mod's output. A malformed package, wrong base, or conflicting resource
rejects the **entire package**, with a diagnostic in `LOG.TXT`. This keeps related
resources such as `BRIT.DAT` / `DATA.OVL` together. The first valid package claiming
a resource wins; subsequent packages touching that resource are skipped even if
the byte ranges do not overlap. Rename/recombine packages or merge them in
Workshop rather than relying on silent last-wins behavior. Names/titles, loaded
counts and rejected packages are logged. Restart after changing the folder.

## Binary layout

All integers are unsigned little-endian 32-bit values; there is no padding.

| Header | Encoding |
| --- | --- |
| Magic/version | Eight ASCII bytes `IMOD0001` |
| Title length | 1–256 UTF-8 bytes |
| Resource count | 1–128 |
| Title | Length bytes; embedded NUL forbidden |

Each resource follows in order:

| Resource field | Encoding |
| --- | --- |
| Filename length | 1–32 |
| Filename | Uppercase ASCII basename, no path components |
| Original size | Exact original resource byte count |
| Result size | 1–8 MiB |
| Original CRC-32 | Standard IEEE CRC-32 of original file bytes |
| Result CRC-32 | Standard IEEE CRC-32 of patched resource bytes |
| Patch count | Number of following spans |
| Each patch | Offset, length, then `length` payload bytes |

The result begins as the original bytes, truncated or zero-extended to result
size. Patches replace bytes in that buffer; they do not insert/shift bytes.
Spans must be nonempty, sorted, nonoverlapping and entirely within the result.
Zero patches are allowed (for example, a truncation). Trailing package bytes,
duplicate resource names and checksum mismatches are rejected.

CRC-32 detects accidental corruption and incompatible originals; it is not an
authentication/signature system. Packages contain game-resource edits, and valid
checksums do not by themselves guarantee that an arbitrary game's data is valid.
Workshop validates the supported resource formats before saving/exporting.

Allowed resources: uppercase basenames ending `.DAT`, `.NPC`, `.TLK`, `.16`,
`.CH`, `.HCS`, `.CBT`, plus `DATA.OVL`, `INIT.GAM`, `INIT.OOL`, `BRIT.OOL` and
`UNDER.OOL`. Drivers, DOS executable overlays other than the Britannia index,
settings, paths and saved-game files cannot be overridden. This is resource data
modding, not replacement executable code.

Limits: 32 MiB encoded package, 32 MiB combined decoded resources per package,
8 MiB per resource, 128 loaded packages, 64 MiB combined mounted resource data.
Failures leave original resources available and do not write to the installation.
Sparse packages omit unchanged resources and unchanged bytes where possible;
compressed graphics edits can necessarily change much of the compressed stream.

The shared parser/checksum implementation is `src/mod/package.c`, used by both
the native Workshop and Impera. `src/mod/runtime.c` mounts packages, and
`FILE_Open` applies overrides only to bare, read-only resource names. Explicit
paths and writable handles retain their existing behavior. The legacy DOS
Makefile has no SDL mod loader; this feature belongs to Impera's native port.
