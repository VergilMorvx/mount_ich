# Improvements

What is wrong with the original `mount_ich`, why it matters, and what the
reimplementation does instead. Ordered by how much it hurts.

Each entry: **what the original does** → **why it is a problem** → **the fix**.

---

## 1. Shell injection on every external command

**Original.** Every command is formatted into a string and handed to `sh -c`
via `system()` or `popen()`. The format strings are literal, e.g.:

```c
snprintf(cmd, 512, "%s -p '%s' 2>/dev/null", apfs_util, dev);
snprintf(cmd, 512, "%s '%s' '%s'", mount_apfs, dev, mnt);
```

**Problem.** Arguments are wrapped in single quotes but never escaped. Any `'`
in a path terminates the quote and the rest becomes shell syntax. The values
being formatted are fixed today, so it is not exploitable *right now* — but the
fragility is baked in, and `$`, `;`, and backticks are all live inside those
quotes. It also means every call costs a `fork` for `sh`, plus a `fork` for the
real binary.

**Fix.** `execv()` with an argv array. No shell, no quoting, no parsing, one
`fork` per command:

```c
char *argv[] = { (char *)kMountApfs, (char *)dev, (char *)mnt, NULL };
run_quiet(argv);
```

**Files.** `src/mount_ich.c` — `run_quiet()`, `run_capture()`.

---

## 2. Mount state queried by forking `mount(8)`

**Original.**

```c
static int is_mounted(const char *mnt)
{
    char out[8192];
    if (run_capture("/sbin/mount", out, sizeof out) != 0) return 0;
    snprintf(a, 64, " on %s ", mnt);
    snprintf(b, 64, " on %s (", mnt);
    return strstr(out, a) || strstr(out, b);
}
```

**Problem.** `is_mounted` is called dozens of times per run — inside
`mount_dev`, inside every strategy step, once per volume at the end. Each call
spawns a shell, spawns `mount`, formats the whole mount table into a pipe, and
greps it with `strstr`. That is on the order of 40+ process spawns just to
answer "is this mounted", on boot, on a phone.

It is also brittle: it depends on the exact spacing `mount(8)` prints. A
different `mount` build, a different locale, or a path containing `" on "`
gives a wrong answer.

**Fix.** `getmntinfo()` — one syscall, structured data, no parsing:

```c
struct statfs *buf = NULL;
int count = getmntinfo(&buf, MNT_NOWAIT);
for (int i = 0; i < count; i++)
    if (strcmp(buf[i].f_mntonname, mnt) == 0) return true;
```

**Files.** `src/mount_ich.c` — `is_mounted()`.

---

## 3. `disk0s1sN` devices are declared but never scanned

**Original.** The pre-flight check accepts either layout:

```c
if (!access("/dev/disk1s1") || !access("/dev/disk0s1s1") ||
    !access("/dev/disk1s2") || !access("/dev/disk0s1s2"))
    proceed();
```

The 16-entry device table in `__DATA_CONST` also lists all eight
`disk0s1sN` devices. But the scanner only ever builds `disk1sN`:

```c
snprintf(dev, 64, "/dev/disk1s%d", i);   // 3..8 only
```

**Problem.** On a checkra1n-style layout where the APFS container appears as
`disk0s1sN`, the tool passes its own pre-flight check and then finds nothing.
It reports `mount failed please report to the ich developer` — which is
exactly the message that tells the user nothing. The device table proves the
author knew about both layouts and simply never wired the second one up.

**Fix.** Both layouts are scanned, for every role lookup:

```c
static const char *const kDevices[] = {
    "/dev/disk1s1",   ... "/dev/disk1s8",
    "/dev/disk0s1s1", ... "/dev/disk0s1s8",
};
```

**Files.** `src/mount_ich.c` — `kDevices[]`, `mount_role()`, `mount_aux_roles()`.
`scripts/mount_ich.sh` does `for d in disk1 disk0s1`.

---

## 4. Sixteen `apfs.util` forks per role lookup

**Original.** `mount_role` walks all 16 devices, running
`apfs.util -p <dev>` on each through a shell until it finds the role it wants.
Called from `strategy_universal` five times plus once inside
`sep_load_and_mount_data`, that is up to 96 `fork`+`exec` pairs before the tool
has mounted anything.

**Problem.** Volume role is a property of the device, not of the question being
asked. Querying the same device repeatedly to answer five different questions
is pure waste.

**Fix.** Structure it as two passes over the device list instead of one
targeted scan per role: one sweep for the named role, one sweep for the three
auxiliary roles. Each device is queried at most twice per strategy, and the
sweep short-circuits on devices that do not exist.

**Files.** `src/mount_ich.c` — `mount_role()`, `mount_aux_roles()`.

---

## 5. Version parsed with `sed` against an XML plist

**Original.**

```c
snprintf(cmd, 512,
    "sed -n '/ProductVersion/{n;s/.*<string>\\([^<]*\\)<\\/string>.*/\\1/p;}' "
    "'%s' 2>/dev/null | head -1", plist);
```

**Problem.** A property list is XML. This regex assumes the `<string>` on the
line *after* `ProductVersion` is the version, and that the tags are on their
own lines. A plist with the key and value on one line, or with an extra
whitespace line between them, returns nothing. It pulls the first line of
output into a 64-byte buffer with no length check beyond `fgets`'s own bound.

**Fix.** `plutil -extract ProductVersion raw` first — it is shipped on every
iOS version this tool targets and parses properly. Only if that fails, fall
back to reading the key/value pair out of the file directly, in-process, with
bounded buffer handling:

```c
char *argv[] = { "/usr/bin/plutil", "-extract", "ProductVersion", "raw",
                 "-o", "-", (char *)plist, NULL };
```

**Files.** `src/mount_ich.c` — `read_version_from()`.

---

## 6. Command failures are invisible

**Original.** `run_capture` returns `0` on any command outcome. It only returns
`-1` if `waitpid` itself fails:

```c
return waitpid_ok ? 0 : -1;
```

**Problem.** A missing `apfs.util`, a device that is present but unreadable, and
a working tool that prints nothing are indistinguishable. Callers then compare
whatever happens to be in the buffer, which may be the previous iteration's
value if the command failed before writing. This is how you get a "successful"
mount of the wrong volume.

**Fix.** Success requires both a clean exit **and** non-empty output:

```c
return clean && len > 0;
```

**Files.** `src/mount_ich.c` — `run_capture()`.

---

## 7. Success test reports half a mount as a full one

**Original.**

```c
ok = (is_dir("/mnt2/root") && is_dir("/mnt2/mobile"))
  || is_dir("/mnt2/containers/Data/System")
  || is_dir("/mnt1/System") || is_dir("/mnt1/Applications");
```

**Problem.** The last clause is unguarded. Even in `full` mode — which is
supposed to bring Data up — a device where only the System volume mounted
reports success, because `/mnt1/System` exists. A caller then walks into
`/mnt2` and finds nothing there.

**Fix.** The `/mnt1` branch only counts when the mode actually stopped at
firmware, which is exactly `safe`:

```c
r.ok = (path_is_dir("/mnt2/root") && path_is_dir("/mnt2/mobile"))
    || path_is_dir("/mnt2/containers/Data/System")
    || (safe_mode && (path_is_dir("/mnt1/System") ||
                      path_is_dir("/mnt1/Applications")));
```

**Files.** `src/mount_ich.c` — `mi_mount()`.

---

## 8. Ten-second banner and no machine-readable output

**Original.** Prints an 8-line ASCII art banner, then `sleep(10)`, then the
copyright line. Progress is a bare `mount done` or nothing at all.

**Problem.** Ten seconds of boot time spent on nothing. And the only output is
prose on stdout — a calling app has no way to know *which* volume failed
without re-checking the filesystem itself.

**Fix.** Banner and sleep removed. `mi_mount()` returns a struct:

```c
typedef struct {
    bool ok;
    bool system_mounted;
    bool data_mounted;
    bool preboot_mounted;
    bool hardware_mounted;
    char version[64];
} mi_result;
```

The CLI prints one line listing the mode, version, and per-volume state.

**Files.** `src/mount_ich.h`, `src/main.c`.

---

## 9. Dead `nvram` key from a typo

**Original.**

```c
run_quiet("/usr/sbin/nvram -d oblit-inprogress");
run_quiet("/usr/sbin/nvram -d oblit-inprogres");   // missing 's'
```

**Problem.** The second call deletes a key that was never set. It costs a
process spawn and does nothing. More importantly it suggests nobody read the
code after writing it — which lowers confidence in the rest of the sequence.

**Fix.** Dropped. One delete, correctly spelled.

**Files.** `src/mount_ich.c` — `sep_load_and_mount_data()`.

---

## 10. `mount_filesystems` executed three times

**Original.** In `strategy_universal`, the same command is issued three times
in a row (visible in the disassembly as three consecutive `bl _run_quiet` on
the same stack string).

**Problem.** Either a copy-paste error or a workaround for a race nobody
diagnosed. It triples the work and, if the script is not idempotent, may do
something on the second and third run that it did not intend.

**Fix.** Run once, and skip it entirely when the Data volume already looks
right:

```c
if (path_is_dir("/mnt2/containers/Data/System") || path_is_dir("/mnt2/root"))
    return;
```

**Files.** `src/mount_ich.c` — `strategy_universal()`.

---

## 11. Unknown arguments silently become `auto`

**Original.** The parser initialises the mode to "auto" and only changes it on
an exact string match. `mount_ich saf` runs a full mount attempt. Only exit
code `0x40` (64) is used for the bad-argument path, and that path is reached
via `universal` falling through, not from a real validation branch.

**Problem.** A typo in a calling script silently escalates from "firmware only"
to "mount Data read-write". For a tool that writes to a filesystem, silently
guessing the wrong mode is the worst possible default.

**Fix.** Explicit parse table, unknown input rejected with exit code 2 and a
usage line.

**Files.** `src/mount_ich.c` — `mi_mode_parse()`, `src/main.c`.

---

## 12. No library boundary

**Original.** Everything lives in `main`. There is no way to use the mount
logic without running the whole program, and no way to ask what happened beyond
the exit code.

**Problem.** Anyone integrating this has to shell out to a binary and parse
stdout.

**Fix.** `mount_ich.h` declares `mi_mount()`, `mi_mode_parse()`, and
`mi_mode_name()`. The engine never touches `stdio` for progress output, never
calls `exit()`, and returns structured state. `src/main.c` is a thin front end
you can drop.

**Files.** `src/mount_ich.h`, `src/main.c`.

---

## 13. Minor

| Original | Fix |
|---|---|
| `mkdir` return values ignored, no `-p` semantics, strays if mountpoint exists as a file | same behaviour, but `prepare_mountpoints()` is a single table-driven loop |
| Success/failure strings differ in wording (`mount done` vs `mount failed please report to the ich developer`) | one consistent one-line format for both |
| `sleep(10)` and banner make the tool untestable in CI | removed |
| Hardcoded `0755` on mountpoints, no mode check | unchanged, but documented |
| Vendor strings and a slur in the output | removed |

---

## What was deliberately kept

Rewriting everything is not an improvement. These are preserved because the
behaviour is load-bearing:

- **The five mountpoints.** `/mnt1`, `/mnt2`, `/mnt4`, `/mnt6`, `/mnt7` are
  what other tools in the ecosystem expect.
- **The mount strategy vocabulary.** `safe` / `full` / `auto` / `ios12` /
  `ios16` / `universal` mean what they meant, so existing scripts keep working.
- **The role-discovery approach.** Asking `apfs.util -p` rather than assuming
  device numbers is the one genuinely good idea in the original.
- **SEP handling order.** `--gigalocker-init`, set nvram, load the firmware,
  clear nvram, then mount Data — the ordering is intentional and was kept.
- **The two-layout success test.** Modern `containers/Data/System` and legacy
  `root` + `mobile` both count, because both are real.
- **The `find` fallback** for `sep-firmware.img4` when `/mnt6/active` does not
  resolve.

---

## Still open

Honest gaps, in the order they matter:

1. **Not tested on device.** The code compiles clean under
   `-Wall -Wextra -Wpedantic` and the logic was checked statement by statement
   against the disassembly, but it has never run on real hardware. First thing
   to do is boot it in a ramdisk on an A12 and diff the resulting mount table
   against the original tool's.
2. **No rollback on partial failure.** If System comes up and Data does not,
   the device is left half-mounted. A `mi_mount` that unmounts what it mounted
   on failure would be strictly better, but unmounting a wrongly-mounted
   System is its own hazard and needs thought.
3. **`hdid` / disk-image image-type handling** is not implemented. It would
   replace the fixed `disk1sN` map with a real query, which also removes
   `strategy_fixed_map()` entirely.
4. **The shell script duplicates the C.** Deliberate, so it works with no
   toolchain, but the two will drift. Worth a test that runs both and compares
   the resulting mount tables.
5. **No `--dry-run`.** For a tool that writes to a filesystem, printing what it
   *would* mount before doing it is cheap insurance. Not implemented because
   the meaning of "would mount" requires probing devices anyway, which is most
   of the work — but the probe-only path already exists as `mount_role` returning
   before `mount_dev`.