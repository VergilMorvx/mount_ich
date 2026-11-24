# mount_ich

A reimplementation of `mount_ich`, a ramdisk-side APFS volume mounter for
jailbroken iOS on A12/A13 hardware.

The tool boots inside a ramdisk and brings up the volumes that a ramdisk leaves
unmounted — `System`, `Data`, `Preboot`, `xART`, `Hardware` — so that the
on-disk system is reachable and writable. Volumes are identified by asking
`apfs.util -p` what role each device carries, rather than by assuming device
numbers.

This repository contains a reverse engineering write-up of an existing build, a
corrected implementation, and an itemised list of the defects found in the
original.

**License:** source-available, restricted. See [LICENSE](LICENSE). Reading and
running the code is permitted; redistribution is not.

## What it does

A ramdisk boots with almost nothing mounted. This tool brings up the volumes
needed to work on the device:

| Mountpoint | Volume | Label |
|---|---|---|
| `/mnt1` | System | `System` |
| `/mnt2` | Data | `Data` |
| `/mnt4` | Hardware | `Hardware` |
| `/mnt6` | Preboot | `Preboot` |
| `/mnt7` | xART | `xART` |

Volumes are identified by asking `apfs.util -p <dev>` what role each device
carries. Both APFS container layouts are handled, so there is no device-number
contract to remember.

## Status

| | |
|---|---|
| Logic parity with original | intentional divergences, see below |
| Shell injection | removed |
| `disk0s1sN` support | added |
| Shell-out on every mount check | removed (`getmntinfo`) |
| Structured result for embedding | added |
| Tests | host checks only (`./tests/test.sh`); device test pending |

### Intentional divergences from the original

These are behaviour changes, not bug fixes. Each is deliberate; the reasoning
is in `IMPROVEMENTS.md`.

| Change | Original | This implementation | Rationale |
|---|---|---|---|
| `nvram` cleanup | two deletes: `oblit-inprogress` and `oblit-inprogres` | one delete: `oblit-inprogress` | the second key is misspelled, so that call was a no-op (`IMPROVEMENTS.md` §9) |
| `mount_filesystems` | executed three times in a row | executed once | idempotent helper; the repeat was likely a copy-paste error (`IMPROVEMENTS.md` §10) |
| Failure signal | `run_capture` returned success on any outcome | requires clean exit and output | a missing tool and a working tool were indistinguishable (`IMPROVEMENTS.md` §6) |
| Success test | `/mnt1` alone counted as success in any mode | `/mnt1` only counts in `safe` mode | a half mount was reported as a full one (`IMPROVEMENTS.md` §7) |
| Unknown arguments | silently treated as `auto` | rejected, exit code 2 | silent escalation to a mode that writes to Data (`IMPROVEMENTS.md` §11) |

Everything else — mountpoints, mode vocabulary, role discovery, SEP ordering,
the two-layout success test, the `find` fallback — is ported as-is.

## Layout

```
mount_ich_open/
├── README.md                  what it is, build, usage, embed, caveats
├── IMPROVEMENTS.md            defects found + why each fix matters
├── CHANGELOG.md               versioned summary of what changed
├── RELEASE_NOTES.md           text for the GitHub release
├── PUBLISHING.md              how to push this to GitHub
├── NOTICE                       authorship and derivation record
├── LICENSE                    source-available, restricted. NOT open source.
├── Makefile / build.sh / .gitignore / .gitattributes
├── docs/
│   ├── REVERSE_ENGINEERING.md full analysis: symbols, pseudocode, constants
│   ├── CHAINED_FIXUPS.md      decoded __DATA_CONST pointer tables
│   └── disassembly.txt        annotated arm64 listing of the original
├── src/
│   ├── mount_ich.h            public interface
│   ├── mount_ich.c            engine
│   └── main.c                 CLI
├── scripts/
│   └── mount_ich.sh           shell-only equivalent
├── samples/
│   └── README.md              hash of the analyzed build; sample not included
├── tests/
│   └── test.sh                host-side checks
└── tools/
    ├── macho_parse.py         minimal Mach-O parser
    ├── disasm.py              annotating disassembler
    ├── cstrings.py            __cstring dumper
    └── constfix.py            chained-fixup pointer decoder
```

## Build

```sh
./build.sh check     # syntax check on any host
./build.sh           # device build: build/mount_ich
```

or

```sh
make check
make
```

Requires clang. For the device build you also need a macOS/Linux SDK with the
arm64 target — cross-compiling from Linux with an iOS toolchain works.

## Test

```sh
./tests/test.sh
```

Covers what is checkable off-device: clean compile, shell script parses, bad
arguments rejected, and the analysis tools reproducing the documented constant
tables from the sample binary.

## Usage

```sh
mount_ich [safe|full|auto|ios12|ios16|universal]
```

| Mode | Behaviour |
|---|---|
| `safe` | Firmware volumes only (`System`, `Preboot`, `xART`, `Hardware`). Never touches Data. |
| `full` | Role discovery plus SEP load and Data mount. |
| `auto` *(default)* | Detect iOS version; `10.x`–`12.x` use the legacy helper path, everything else the universal path. |
| `ios12` | Legacy `mount_root` / `mount_data` / `mount_data2` path. |
| `ios16` | Fixed device map (`disk1s1/3/5/6`) plus SEP load. |
| `universal` | Role-based discovery, widest path. |

Exit codes: `0` success, `1` failure, `2` bad argument. A one-line summary goes
to stdout on success and stderr on failure, listing which volumes came up.

No argument defaults to **universal**.

## Embedding

The engine is decoupled from `main`. Compile `src/mount_ich.c` into your app
and call `mi_mount()`:

```c
#include "mount_ich.h"

mi_result r = mi_mount(MI_MODE_AUTO, NULL);
if (!r.ok) {
    fprintf(stderr, "mount failed: system=%d data=%d preboot=%d hardware=%d (ios %s)\n",
            r.system_mounted, r.data_mounted,
            r.preboot_mounted, r.hardware_mounted,
            r.version[0] ? r.version : "unknown");
}
```

Pass a version string as the second argument to skip plist detection:

```c
mi_result r = mi_mount(MI_MODE_UNIVERSAL, "16.5");
```

## Caveats

- **Needs root and a ramdisk.** Outside one, `any_block_device_present()`
  fails and `mi_mount` returns immediately with `ok == false`. It is safe to
  call; it just does nothing.
- **Not thread safe.** Call once, at boot.
- **Not a bypass tool.** It mounts volumes. It does not touch activation,
  iCloud, or baseband.
- **Untested on device.** The logic is verified against the disassembly, and
  the code compiles clean under `-Wall -Wextra -Wpedantic`, but it has not been
  run on real hardware. See `IMPROVEMENTS.md` §"Still open".

## License

Source-available, restricted. Not OSI open source. See [LICENSE](LICENSE) for
the terms; the short version is that reading and running the code is permitted
and redistribution is not.

Copyright (c) 2026 vergilmorvx. All rights reserved.

This is an independent reimplementation of observed behaviour. It contains no
code from the original binary — only logic derived from disassembly, which is
documented in `docs/REVERSE_ENGINEERING.md` so the derivation can be checked
independently.

Authorship and derivation provisions are recorded in [NOTICE](NOTICE).