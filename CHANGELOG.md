# Changelog

## 1.0.0

Initial open release. Clean-room reimplementation of `mount_ich`
(sha256 `83524dcf…b613`).

### Added

- `mi_mount()` engine with structured `mi_result` return
- `mi_mode_parse()` / `mi_mode_name()` for CLI-style mode strings
- CLI front end (`src/main.c`) preserving the original mode vocabulary
- Shell-only equivalent (`scripts/mount_ich.sh`) for environments with no
  toolchain
- Analysis toolchain: Mach-O parser, annotating disassembler, cstring dumper,
  chained-fixup decoder
- Full reverse engineering write-up with per-function pseudocode

### Changed from the original

- Every external command runs through `execv` with an argv array instead of
  `sh -c`; no shell involvement anywhere
- Mount state from `getmntinfo()` instead of forking `mount(8)` on each check
- Both APFS container layouts scanned (`disk1sN` and `disk0s1sN`)
- Role lookups restructured into sweep passes instead of one full scan per role
- Version read with `plutil -extract` first, in-process fallback second
- `run_capture` requires a clean exit **and** output before reporting success
- Success test no longer accepts `/mnt1` alone outside `safe` mode
- Unknown arguments rejected with exit code 2 instead of silently becoming
  `auto`
- One-line structured status output instead of a banner plus prose
- `mount_filesystems` runs once, and only when the Data layout is wrong
- Dead `nvram -d oblit-inprogres` call (typo) removed
- 10-second banner sleep removed

### Known issues

- Not tested on real hardware. Compiles clean under
  `-Wall -Wextra -Wpedantic`; behaviour verified against the disassembly only.
- No rollback on partial mount failure.
- `hdid` / disk-image type handling not implemented; `strategy_fixed_map()`
  still uses hardcoded device numbers.
- The C and shell implementations are maintained separately and will drift.