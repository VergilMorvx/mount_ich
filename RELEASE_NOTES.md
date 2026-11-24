# Release 1.0.0

First release. Reverse engineering write-up plus a corrected implementation of
`mount_ich`, a ramdisk-side APFS volume mounter for jailbroken iOS on A12/A13
hardware.

**Sample analysed**

| | |
|---|---|
| File | `samples/mount_ich` |
| SHA-256 | `83524dcf914a408a7cb8c740d4f37fd8f4f6a3be010db098f36568489b731613` |
| Size | 52,048 bytes |
| Format | Mach-O 64-bit executable, arm64 |

## What's in the box

- `src/` — the implementation: engine, header, CLI
- `scripts/mount_ich.sh` — same logic in POSIX sh for environments with no toolchain
- `docs/REVERSE_ENGINEERING.md` — full analysis, per-function pseudocode
- `docs/CHAINED_FIXUPS.md` — decoded `__DATA_CONST` tables with addresses
- `docs/disassembly.txt` — annotated arm64 listing of the original
- `IMPROVEMENTS.md` — 13 numbered defects and their fixes
- `tools/` — the Python harness that produced the analysis
- `tests/test.sh` — host-side checks
- `NOTICE` — authorship and derivation record

## Fixed relative to the original

Thirteen items, detailed in `IMPROVEMENTS.md`. The four that matter:

1. **Shell injection.** The original formats every command into a string and
   hands it to `sh -c`. Arguments are quote-wrapped but never escaped. This
   release uses `execv` with argv arrays — no shell anywhere.
2. **`mount(8)` forked on every mount check.** ~40 process spawns per run just
   to answer "is this mounted". Replaced with one `getmntinfo` syscall.
3. **`disk0s1sN` never scanned.** The device table lists all eight
   `disk0s1sN` entries and the pre-flight check accepts them, but the scanner
   only ever builds `disk1sN`. On a checkra1n-style layout the tool passes its
   own pre-flight and then finds nothing. Both layouts are now scanned.
4. **Command failures invisible.** `run_capture` returned `0` on any outcome,
   including failure. Success now requires a clean exit *and* output.

Also: `sed` regex replaced with `plutil -extract`; success test no longer
accepts a half mount; 10-second banner sleep removed; unknown arguments rejected
instead of silently becoming `auto`; engine separated from CLI behind
`mi_mount()` / `mi_result`.

### Behaviour divergences

Two changes alter what the program does on a device rather than fixing
something. They are listed separately because they are not improvements:

- **`nvram` cleanup** issues one delete, not two. The original's second call
  targeted the misspelled key `oblit-inprogres` and was almost certainly a
  no-op.
- **`mount_filesystems`** runs once, not three times. The original's repeat was
  probably a copy-paste error.

Both are believed harmless. Both are recorded so that "ported faithfully" is not
claimed alongside them.

## Known issues

- **Not tested on real hardware.** Compiles clean under
  `-Wall -Wextra -Wpedantic`; behaviour verified against the disassembly only.
  Boot it in a ramdisk on an A12 and diff the mount table against the original
  before trusting it.
- **No rollback on partial failure.** A half-mounted device is left as is.
- **`hdid` / disk-image type handling not implemented**, so `strategy_fixed_map()`
  still uses hardcoded device numbers.
- **The C and shell implementations are maintained separately** and will drift.
- **`tests/test.sh` has not been run end-to-end.** Written for a POSIX host with
  `clang`, `sh`, and `python3`; not executable in the environment it was written
  in.

## License

Source-available, restricted. Not OSI open source. See `LICENSE`. Reading and
running the code is permitted; redistribution and incorporation into other
projects are not. Authorship and derivation provisions are recorded in
`NOTICE`.

Copyright (c) 2026 vergilmorvx. All rights reserved.
