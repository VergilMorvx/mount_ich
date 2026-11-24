#!/usr/bin/env python3
"""Decode __DATA_CONST pointer tables.

dyld chained fixups encode a pointer as (next << 51) | (target & 0x7FFFFFFFFFFF)
for the arm64e arm64e-style 'DYLD_CHAINED_PTR_ARM64E' format, and as
(next << 51) | offset_from_image_base for plain 'DYLD_CHAINED_PTR_64'.
In both cases the low 32 bits of the stored value are the unsigned offset from
the image base, which is what resolves string tables in this binary.

Masking to the low 32 bits and adding the image base yields the virtual address
of the pointee. That is how the device table (_mount_role.devs) and the plist
table (_read_ios_version.plists) were recovered.

Usage:
    python3 constfix.py <path>
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from macho_parse import MachO

OFFSET_MASK = 0xFFFFFFFF


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    m = MachO(argv[1])
    image_base = m.segments['__TEXT'][0] & ~0xFFFFFFFF

    for name in ('__got', '__const'):
        sec = m.section(name)
        if not sec:
            continue

        print('# %s,%s  addr=0x%x size=0x%x'
              % (sec['seg'], sec['name'], sec['addr'], sec['size']))

        for off in range(0, sec['size'], 8):
            raw = struct.unpack_from('<Q', m.d, sec['offset'] + off)[0]
            addr = sec['addr'] + off
            target = (image_base + (raw & OFFSET_MASK)) & 0xFFFFFFFFFFFFFFFFF

            if not target:
                print('  0x%08x = 0x%016x  (null)' % (addr, raw))
                continue

            text = m.read_cstring(target)
            if text is not None:
                print('  0x%08x = 0x%016x -> 0x%x %r'
                      % (addr, raw, target, text))
            else:
                print('  0x%08x = 0x%016x -> 0x%x'
                      % (addr, raw, target))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))