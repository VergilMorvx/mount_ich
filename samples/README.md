# Sample

The binary this analysis was performed on is **not** included in this
repository. It is a third-party artifact and is not covered by this project's
license.

To verify the analysis yourself, supply your own copy at `samples/mount_ich`
and confirm it matches:

| Field | Value |
|---|---|
| Size | 52,048 bytes |
| SHA-256 | `83524dcf914a408a7cb8c740d4f37fd8f4f6a3be010db098f36568489b731613` |
| Format | Mach-O 64-bit executable, arm64 |

```sh
sha256sum samples/mount_ich      # Linux
shasum -a 256 samples/mount_ich  # macOS
```

If the hash matches, the analysis applies. Then:

```sh
cd tools
python3 macho_parse.py ../samples/mount_ich
python3 cstrings.py    ../samples/mount_ich
python3 constfix.py    ../samples/mount_ich
python3 disasm.py      ../samples/mount_ich > listing.txt
```

Every claim in `../docs/REVERSE_ENGINEERING.md` is a line in that listing. A
pre-generated copy is committed at `../docs/disassembly.txt`, so you can check
the reasoning without running anything.

If the hash does **not** match, stop. You have a different build, and the
addresses, tables, and pseudocode here may not describe it.