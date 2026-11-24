#!/usr/bin/env python3
"""Minimal arm64 Mach-O parser. No external dependencies.

Usage:
    python3 macho_parse.py <path>

Prints sections plus the defined and undefined symbol tables.
"""

import struct
import sys


class MachO:
    def __init__(self, path):
        with open(path, 'rb') as fh:
            self.d = fh.read()

        (magic, self.cputype, self.cpusub, self.filetype,
         self.ncmds, self.sizeofcmds, self.flags, _) = \
            struct.unpack_from('<IiiIIIII', self.d, 0)
        assert magic == 0xFEEDFACF, 'not a 64-bit Mach-O: %#x' % magic

        self.segments = {}
        self.sections = []
        self.symbols = []
        self.symtab = None

        off = 32
        for _ in range(self.ncmds):
            cmd, size = struct.unpack_from('<II', self.d, off)

            if cmd == 0x19:                       # LC_SEGMENT_64
                name = self.d[off + 8:off + 24].rstrip(b'\0').decode()
                vmaddr, vmsize, fileoff, filesize = \
                    struct.unpack_from('<QQQQ', self.d, off + 24)
                nsects = struct.unpack_from('<I', self.d, off + 64)[0]
                self.segments[name] = (vmaddr, vmsize, fileoff, filesize)

                so = off + 72
                for _ in range(nsects):
                    sn = self.d[so:so + 16].rstrip(b'\0').decode()
                    seg = self.d[so + 16:so + 32].rstrip(b'\0').decode()
                    addr, sz = struct.unpack_from('<QQ', self.d, so + 32)
                    (soff, _align, _reloff, _nreloc,
                     flags, _, _, _) = struct.unpack_from('<IIIIIIII',
                                                          self.d, so + 48)
                    self.sections.append(dict(name=sn, seg=seg, addr=addr,
                                              size=sz, offset=soff,
                                              flags=flags))
                    so += 80

            elif cmd == 0x2:                      # LC_SYMTAB
                self.symtab = struct.unpack_from('<IIII', self.d, off + 8)

            off += size

        self._read_symtab()

    def _read_symtab(self):
        if not self.symtab:
            return
        symoff, nsyms, stroff, strsize = self.symtab
        strs = self.d[stroff:stroff + strsize]

        for i in range(nsyms):
            strx, ntype, sect, desc, value = \
                struct.unpack_from('<IBBHQ', self.d, symoff + i * 16)
            if strx == 0:
                continue
            end = strs.find(b'\0', strx)
            self.symbols.append(dict(
                name=strs[strx:end].decode('utf-8', 'replace'),
                type=ntype, sect=sect, desc=desc, addr=value))

    def section(self, name):
        return next((s for s in self.sections if s['name'] == name), None)

    def text_section(self):
        return self.section('__text')

    def read_cstring(self, addr):
        """Read a NUL-terminated string at a virtual address."""
        for s in self.sections:
            if s['addr'] <= addr < s['addr'] + s['size']:
                fo = s['offset'] + (addr - s['addr'])
                end = self.d.find(b'\0', fo)
                return self.d[fo:end].decode('utf-8', 'replace')
        return None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    m = MachO(argv[1])

    print('# sections')
    for s in m.sections:
        print('  %-8s,%-16s addr=0x%08x size=0x%06x off=0x%06x flags=0x%x'
              % (s['seg'], s['name'], s['addr'], s['size'],
                 s['offset'], s['flags']))

    print('# functions')
    for s in m.symbols:
        if s['type'] & 0xE == 0xE and s['addr']:
            print('  0x%08x %s' % (s['addr'], s['name']))

    print('# data')
    for s in m.symbols:
        if s['type'] & 0xE != 0xE and s['addr']:
            print('  0x%08x %s' % (s['addr'], s['name']))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))