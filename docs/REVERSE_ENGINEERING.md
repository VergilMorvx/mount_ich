# Reverse Engineering — `mount_ich`

Everything here comes from the binary. No vendor docs, no guessing.

| Field | Value |
|---|---|
| File | `mount_ich` |
| Size | 52,048 bytes |
| SHA-256 | `83524dcf914a408a7cb8c740d4f37fd8f4f6a3be010db098f36568489b731613` |
| Format | Mach-O 64-bit executable, arm64, single arch, no fat header |
| Linker | ld64, chained fixups (`LC_DYLD_CHAINED_FIXUPS`), no dyld info opcodes |
| Load commands | 18, `sizeofcmds` 0x4C0 |
| RPaths | `/usr/lib/dyld` |
| Libraries | `libSystem.B.dylib` only |
| `__TEXT,__text` | `0x100004000`, 0x11BC bytes |
| `__TEXT,__stubs` | `0x1000051BC`, 0x120 bytes (26 stubs) |
| `__TEXT,__cstring` | `0x1000052DC`, 0x8D5 bytes (~105 literals) |
| `__TEXT,__unwind_info` | `0x100005BB4`, 0x88 bytes |
| `__DATA_CONST,__got` | `0x100008000`, 0xD0 bytes |
| `__DATA_CONST,__const` | `0x1000080D0`, 0xA0 bytes |
| Symbols | 13 defined, 26 undefined (all stock libc) |
| Stripped | No — full function symbols present |
| C++ / ObjC / Swift | None. Plain C, no runtime, no framing |
| Anti-analysis | None. No packs, no encryption, no obfuscation, no `ptrace` checks |

Written in plain C with stock clang. Trivially readable — which is why the
behaviour is fully recoverable and every claim below can be checked line by
line. The reimplementation is not a transcription: it restructures execution,
removes the shell layer, and changes two observable behaviours. See
`../IMPROVEMENTS.md` and the divergence table in `../README.md`.

**Contents:** [1 What it is](#1-what-it-is) · [2 Command line](#2-command-line) ·
[3 Execution order](#3-execution-order) · [4 Functions](#4-recovered-functions) ·
[5 Constants](#5-constant-tables) · [6 Pseudocode](#6-function-pseudocode) ·
[7 Behavioural notes](#7-behavioural-notes) · [8 Method](#8-method-note) ·
[9 Verify it yourself](#9-verify-it-yourself)
[9 Verify it yourself](#9-verify-it-yourself)

---

## 1. What it is

A **ramdisk-side filesystem mounter** for jailbroken iOS on A12/A13 hardware.

It runs *inside* an already-booted ramdisk environment, as root, and its only
job is to bring up the APFS volumes that a ramdisk leaves unmounted: `System`,
`Data`, `Preboot`, `xART`, `Hardware`.

It does **not** bypass anything on its own. It does not touch activation,
iCloud, or baseband. It is the plumbing underneath a tool that does — it makes
the on-disk system reachable and writable so something else can edit it. The
banner strings confirm the family: it ships in an iCloud-bypass toolkit.

Nothing here is device-specific magic. The logic is generic APFS volume
discovery, which is why it is fully reproducible.

---

## 2. Command line

```
mount_ich [safe|full|auto|ios12|ios16|universal]
```

| Arg | Internal id | Behaviour |
|---|---|---|
| *(none)* | 0 / auto | Same as `auto` |
| `safe` | 1 | Mount firmware volumes only, never touch Data |
| `full` | 2 | Fixed device map + SEP load |
| `auto` | 0 | Detect iOS version, pick a strategy |
| `ios12` | 3 | `mount_root` / `mount_data` / `mount_data2` path |
| `ios16` | 4 | Fixed device map (`disk1s1/3/5/6`) + SEP load |
| `universal` | 5 | Role-based discovery, the widest path |
| `-h`, `--help`, anything unknown | — | Prints usage, `exit(0)` |

`auto` is the fall-through default: the parser starts at "auto" and only
changes strategy on an exact match, so an unrecognised argument silently
becomes `auto` rather than an error.

---

## 3. Execution order

1. `setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin:/System/Library/Filesystems/apfs.fs", 1)`
2. Prints an 8-line ASCII banner, then **`sleep(10)`**
3. `mkdir` on `/mnt1 /mnt2 /mnt4 /mnt6 /mnt7 /tmp`, mode `0755`
4. `mount -uw /`, then `mount -u -w /` — make the ramdisk's own root writable
5. Pre-flight: if none of `disk1s1`, `disk0s1s1`, `disk1s2`, `disk0s1s2` exist,
   print the failure line and return 1
6. Mount `System` by role, falling back to `/dev/disk1s1`
7. Sweep devices for `Preboot` / `xART` / `Hardware`
8. Mount `Preboot` if not already up
9. Read the iOS version from plist (two candidates, §5)
10. Dispatch on strategy (§6)
11. Verify the expected directory layout, print `mount done` or the failure
    line

---

## 4. Recovered functions

| Address | Symbol | Role |
|---|---|---|
| `0x100004000` | `_main` | Banner, arg parse, mkdirs, pre-flight, dispatch, verify |
| `0x100004744` | `_done_fail` | Prints the failure line, tail-calls `puts` |
| `0x100004750` | `_mount_role` | Find the device carrying an APFS role, mount it |
| `0x100004844` | `_mount_dev` | Mount one device on one mountpoint |
| `0x100004914` | `_mount_roles_disk1_scan` | Scan `disk1s3..s8` for Preboot/xART/Hardware |
| `0x100004A88` | `_is_mounted` | Grep `mount(8)` output |
| `0x100004B84` | `_strategy_universal` | The wide role-based path |
| `0x100004CF0` | `_run_quiet` | `fork`+`exec` `sh -c`, output to `/dev/null` |
| `0x100004DC0` | `_run_capture` | `popen`-style pipe capture, first line, strips CR/LF |
| `0x100004F80` | `_sep_load_and_mount_data` | SSV/SEP setup, then mount Data |
| `0x1000080D0` | `_mount_role.devs` | 16-entry `char*` device table |
| `0x100008158` | `_read_ios_version.plists` | 2-entry `char*` plist table |

Recovered libc imports (26): `setenv, putchar, puts, mkdir, access, stat,
snprintf, strcmp, strlen, strstr, fork, execl, _exit, pipe, read, open, dup2,
close, waitpid, fopen, fgets, fclose, exit, __stack_chk_fail`, plus the chained
fixup target for `sleep`.

---

## 5. Constant tables

Decoded from `__DATA_CONST,__const`. Pointers are chained fixups; masking to
the low 32 bits yields the target.

**`_mount_role.devs` — 16 devices, in scan order:**

```
/dev/disk1s1   /dev/disk1s2   /dev/disk1s3   /dev/disk1s4
/dev/disk1s5   /dev/disk1s6   /dev/disk1s7   /dev/disk1s8
/dev/disk0s1s1 /dev/disk0s1s2 /dev/disk0s1s3 /dev/disk0s1s4
/dev/disk0s1s5 /dev/disk0s1s6 /dev/disk0s1s7 /dev/disk0s1s8
```

Note the 16th entry is a 2-element array in the data section
(`0x100008148`), consistent with a `char *devs[][2]` or a padded table.

**`_read_ios_version.plists` — version candidates, in order:**

```
/mnt6/cryptex1/current/SystemVersion.plist
/mnt1/System/Library/CoreServices/SystemVersion.plist
```

**Mountpoints**

| Path | Volume role |
|---|---|
| `/mnt1` | `System` |
| `/mnt2` | `Data` |
| `/mnt4` | `Hardware` |
| `/mnt6` | `Preboot` |
| `/mnt7` | `xART` |

**APFS role names matched:** `System`, `Preboot`, `Data`, `xART`, `Hardware`.

**Commands invoked**

```
/sbin/mount -uw /
/sbin/mount -u -w /
/sbin/mount
/sbin/mount_apfs <dev> <mnt>
/System/Library/Filesystems/apfs.fs/apfs.util -p <dev>
/usr/libexec/seputil --gigalocker-init
/usr/libexec/seputil --load <path>
/usr/sbin/nvram oblit-inprogress=1 rev=2
/usr/sbin/nvram -d oblit-inprogress
/usr/sbin/nvram -d oblit-inprogres      (typo, dead)
/bin/sh -c <string>
find /mnt6 -iname 'sep-firmware.img4' 2>/dev/null | head -1
sed -n '/ProductVersion/{n;s/.*<string>\([^<]*\)<\/string>.*/\1/p;}' <plist>
/usr/bin/mount_root
/usr/bin/mount_data
/usr/bin/mount_data2
/usr/bin/mount_filesystems
/usr/bin/mount_filesystems.original
```

---

## 6. Function pseudocode

### `run_quiet(cmd)`

```c
pid = fork();
if (pid < 0) return -1;
if (pid == 0) {
    fd = open("/dev/null", O_RDWR);
    dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
    if (fd >= 3) close(fd);
    execl("/bin/sh", "sh", "-c", cmd, NULL);
    _exit(127);
}
waitpid(pid, &status, 0);
return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
```

### `run_capture(cmd, buf, buflen)`

```c
if (!buf) return -1;
buf[0] = 0;
pipe(fds); pid = fork();
/* child: dup2(fds[1], STDOUT), execl("/bin/sh","sh","-c",cmd,NULL), _exit(127) */
/* parent: close(fds[1]); read until full or EOF; buf[n] = 0 */
/* strip trailing bytes in { 0x0A, 0x0D, 0x20 } */
waitpid(pid, &status, 0);
return waitpid_ok ? 0 : -1;
```

Returns `0` on **any** command outcome, including failure. Callers rely on the
buffer contents, not the return code.

### `is_mounted(mnt)`

```c
if (run_capture("/sbin/mount", out, 8192) != 0) return 0;
snprintf(a, 64, " on %s ", mnt);
snprintf(b, 64, " on %s (", mnt);
return strstr(out, a) || strstr(out, b);
```

Shells out to `mount(8)` on every call. No `getmntinfo`, no `statfs`.

### `mount_dev(dev, mnt)`

```c
if (access(dev, F_OK)) return -1;      /* device must exist */
if (is_mounted(mnt))   return  0;      /* already up */
snprintf(cmd, 512, "/sbin/mount_apfs '%s' '%s'", dev, mnt);
run_quiet(cmd);
return is_mounted(mnt) ? 0 : -1;
```

### `mount_role(role, mnt)`

```c
for (i = 0; i < 16; i++) {
    buf[0] = 0;
    if (access(devs[i], F_OK) == 0) {
        snprintf(cmd, 512, "%s -p '%s' 2>/dev/null", apfs_util, devs[i]);
        run_capture(cmd, buf, 64);
    }
    if (strcmp(buf, role) == 0)
        return mount_dev(devs[i], mnt);
}
return -1;
```

`apfs.util -p <dev>` prints the volume role. That is the entire discovery
mechanism — up to 16 `fork`/`exec` pairs per lookup.

### `mount_roles_disk1_scan()`

```c
for (i = 3; i <= 8; i++) {
    snprintf(dev, 64, "/dev/disk1s%d", i);
    if (access(dev, F_OK) == 0) run_capture(apfs_role_cmd(dev), buf, 64);
    if      (strcmp(buf, "Preboot"))  mount_dev(dev, "/mnt6");
    else if (strcmp(buf, "xART"))     mount_dev(dev, "/mnt7");
    else if (strcmp(buf, "Hardware")) mount_dev(dev, "/mnt4");
}
```

Hardcoded to `disk1sN`. `disk0s1sN` is never scanned here despite being in the
device table.

### `sep_load_and_mount_data()`

```c
if (access("/usr/libexec/seputil", F_OK) == 0)
    run_quiet("/usr/libexec/seputil --gigalocker-init");

if (access("/usr/sbin/nvram", F_OK) == 0)
    run_quiet("/usr/sbin/nvram oblit-inprogress=1 rev=2");

sep[0] = 0;
if (access("/mnt6/active", F_OK) == 0) {
    f = fopen("/mnt6/active", "r");
    if (f && fgets(active, 256, f)) {
        strip_trailing_newline(active);
        snprintf(sep, 512, "/mnt6/%s/usr/standalone/firmware/sep-firmware.img4", active);
        if (access(sep, F_OK) != 0) sep[0] = 0;    /* fall through to find */
    }
    if (f) fclose(f);
}

if (!sep[0])
    run_capture("find /mnt6 -iname 'sep-firmware.img4' 2>/dev/null | head -1", sep, 512);

if (sep[0] && access("/usr/libexec/seputil", F_OK) == 0) {
    snprintf(cmd, 800, "%s --load '%s'", seputil, sep);
    run_quiet(cmd);
}

if (access("/usr/sbin/nvram", F_OK) == 0) {
    run_quiet("/usr/sbin/nvram -d oblit-inprogress");
    run_quiet("/usr/sbin/nvram -d oblit-inprogres");   /* typo is in the original */
}

if (mount_role("Data", "/mnt2") != 0)
    mount_dev("/dev/disk1s2", "/mnt2");
```

### `read_ios_version(ver, 64)`

```c
for (i = 0; i < 2; i++) {
    if (access(plists[i], F_OK)) continue;
    snprintf(cmd, 512,
        "sed -n '/ProductVersion/{n;s/.*<string>\\([^<]*\\)<\\/string>.*/\\1/p;}' "
        "'%s' 2>/dev/null | head -1", plists[i]);
    run_capture(cmd, ver, 64);
    if (ver[0]) break;
}
```

### `strategy_universal(skip_data)`

```c
if (mount_role("System", "/mnt1") != 0) mount_dev("/dev/disk1s1", "/mnt1");
mount_roles_disk1_scan();
if (!is_mounted("/mnt6")) mount_role("Preboot",  "/mnt6");
if (!is_mounted("/mnt7")) mount_role("xART",     "/mnt7");
if (!is_mounted("/mnt4")) mount_role("Hardware", "/mnt4");

if (skip_data) return;                     /* the "safe" mode exit */

sep_load_and_mount_data();

if (!is_dir("/mnt2/containers/Data/System") && !is_dir("/mnt2/root")) {
    if (access("/usr/bin/mount_filesystems", F_OK) == 0)
        run_quiet("/bin/sh /usr/bin/mount_filesystems");        /* run 3x */
    else if (access("/usr/bin/mount_filesystems.original", F_OK) == 0)
        run_quiet("/bin/sh /usr/bin/mount_filesystems.original");
}
```

### Fixed device map (`full` / `ios16`)

```c
mount_dev("/dev/disk1s1", "/mnt1");
mount_dev("/dev/disk1s6", "/mnt6");
mount_dev("/dev/disk1s3", "/mnt7");
mount_dev("/dev/disk1s5", "/mnt4");
sep_load_and_mount_data();
```

### `main` — dispatch

```c
if (mode != ios12) {
    if (mount_role("System", "/mnt1") != 0) mount_dev("/dev/disk1s1", "/mnt1");
    mount_roles_disk1_scan();
    if (!is_mounted("/mnt6")) mount_role("Preboot", "/mnt6");
}
read_ios_version(ver);

switch (mode) {
case 1: strategy_universal(1); break;              /* safe */
case 2: strategy_fixed_map_equivalent(); break;    /* full */
case 3: legacy_mount_root_path(); break;           /* ios12 */
case 4: fixed_device_map(); break;                 /* ios16 */
case 5: strategy_universal(0); break;              /* universal */
case 0:
    if (ver[0]=='1' && (ver[1]=='0'||ver[1]=='1'||ver[1]=='2') && ver[2]=='.')
        legacy_mount_root_path();
    else if (!strncmp(ver,"16.4",4) || !strncmp(ver,"16.5",4))
        strategy_universal(0);
    else
        strategy_universal(0);
}
```

`16.4` and `16.5` are compared as packed words (`0x342e3631`, `0x352e3631`)
against the first four bytes of the version buffer.

### Success test

```c
ok = (is_dir("/mnt2/root") && is_dir("/mnt2/mobile"))   /* legacy Data layout */
  || is_dir("/mnt2/containers/Data/System")             /* modern Data layout */
  || is_dir("/mnt1/System") || is_dir("/mnt1/Applications");
```

The last two clauses are unguarded — see `../IMPROVEMENTS.md` §7.

---

## 7. Behavioural notes

- The tool trusts `apfs.util -p` output byte-for-byte in `strcmp`. Any trailing
  whitespace in that tool's output breaks role matching entirely.
- `run_capture` truncates at 64 bytes for role queries and the version, but the
  `find` fallback uses 512. A long path from `find` is safe; a long role name
  would not be.
- The `mount_filesystems` invocation is unconditional on the *existence* of the
  binary but conditional on the Data layout, so on a device where Data mounted
  correctly the script is skipped — the three-run behaviour only shows up when
  something is already wrong.
- The pre-flight returns failure if **no** block devices are visible, but a
  partial device set (only `disk0s1s2`, say) proceeds and then cannot find
  anything, producing the same message.
- Nothing in the binary reads or writes `fstab`, entitlements, or a config
  file. It is entirely argv + environment + the live filesystem.

---

## 8. Method note

Recovered without Hopper or Ghidra. Both available REA providers refused —
Ghidra was 12.1.2 against a required 12.1.4, and Hopper does not run on
Windows — so the analysis went straight to the bytes. Analysis run 2026-11-22.

The steps:

1. `open_binary` → confirmed Mach-O / arm64 / not fat
2. Mach-O header walk: 4 segments, 6 sections, `LC_SYMTAB` (39 symbols across
   the full table), `LC_DYSYMTAB` with an empty indirect symbol table,
   `LC_DYLD_CHAINED_FIXUPS`
3. `__cstring` dumped address by address; ~105 literals, all readable
4. `__DATA_CONST,__const` decoded through chained fixups → device table and
   plist table
5. Full `__text` linear disassembly with capstone, every `adrp`/`add`, `ldr`,
   and `bl` resolved against the symbol table
6. Function boundaries from the symbol table; control flow read by hand

The only judgement calls are the names given to recovered libc imports
(`puts` vs `printf`, `stat` vs `lstat`), and those are pinned by argument
usage and the stub order.

---

## 9. Verify it yourself

Requires `python3` and `capstone` (`pip install capstone`).

```sh
cd tools
python3 macho_parse.py ../samples/mount_ich          # sections + symbols
python3 cstrings.py    ../samples/mount_ich          # all __cstring literals
python3 constfix.py    ../samples/mount_ich          # decoded constant tables
python3 disasm.py      ../samples/mount_ich > listing.txt

grep -A40 '_sep_load_and_mount_data:' listing.txt
grep -A20 '_mount_roles_disk1_scan:'   listing.txt
```

Every claim in this document is a line in that listing. `docs/disassembly.txt`
is a pre-generated copy so you do not have to run anything to check the
reasoning.

### Chained fixup decoding

`__DATA_CONST` pointers carry encoded high bits, e.g. `0x0010000100005330`.
Mask to the low 32 bits for the target address: `0x100005330`. That is what
`tools/constfix.py` does to resolve `_mount_role.devs` and
`_read_ios_version.plists`.