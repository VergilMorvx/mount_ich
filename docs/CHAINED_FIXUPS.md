# Chained fixup decoding (verified addresses)

`__DATA_CONST` stores pointers in dyld chained-fixup form:

```
raw = 0x0010000100005330
      ^^^^^^^^ ^^^^^^^^
      next/or  offset from image base
```

- High bits (bit 51 and above) are the chain stride / next pointer.
- `raw & 0xFFFFFFFF` is the unsigned offset from the image base.
- Image base here is `0x100000000`.
- Target = image base + offset. Verify: `0x100000000 + 0x5330 = 0x100005330`.

`tools/constfix.py` applies exactly that and was used to produce the tables in
`../docs/REVERSE_ENGINEERING.md`.

## Resulting tables

`_mount_role.devs` at `0x1000080D0`, 16 entries, then a null terminator at
`0x100008150`:

| Address | Target | Value |
|---|---|---|
| 0x1000080D0 | 0x100005330 | `/dev/disk1s1` |
| 0x1000080D8 | 0x10000562A | `/dev/disk1s2` |
| 0x1000080E0 | 0x100005676 | `/dev/disk1s3` |
| 0x1000080E8 | 0x100005683 | `/dev/disk1s4` |
| 0x1000080F0 | 0x100005690 | `/dev/disk1s5` |
| 0x1000080F8 | 0x10000569D | `/dev/disk1s6` |
| 0x100008100 | 0x1000056AA | `/dev/disk1s7` |
| 0x100008108 | 0x1000056B7 | `/dev/disk1s8` |
| 0x100008110 | 0x10000561B | `/dev/disk0s1s1` |
| 0x100008118 | 0x100005637 | `/dev/disk0s1s2` |
| 0x100008120 | 0x1000056C4 | `/dev/disk0s1s3` |
| 0x100008128 | 0x1000056D3 | `/dev/disk0s1s4` |
| 0x100008130 | 0x1000056E2 | `/dev/disk0s1s5` |
| 0x100008138 | 0x1000056F1 | `/dev/disk0s1s6` |
| 0x100008140 | 0x100005700 | `/dev/disk0s1s7` |
| 0x100008148 | 0x10000570F | `/dev/disk0s1s8` |
| 0x100008150 | — | null |

`_read_ios_version.plists` at `0x100008158`, 2 entries, then a null terminator
at `0x100008168`:

| Address | Target | Value |
|---|---|---|
| 0x100008158 | 0x1000057BA | `/mnt6/cryptex1/current/SystemVersion.plist` |
| 0x100008160 | 0x1000057E5 | `/mnt1/System/Library/CoreServices/SystemVersion.plist` |
| 0x100008168 | — | null |

Note `_mount_role.devs` sits immediately after a null terminator, which is
what a `char *devs[] = { ... };` followed by a separate `char *plists[] = { ... };`
looks like in `.data` when the linker places them adjacently.

## Confirming it

```sh
cd tools
python3 constfix.py ../samples/mount_ich | tail -22
```

The 16 `/dev/disk1sN` + `/dev/disk0s1sN` entries and the 2 plist paths should
come out identical to the tables above.