#!/usr/bin/env python3
"""Disassemble __text of the arm64 Mach-O and annotate cstring/adrp/bl targets."""
import sys, struct
sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from macho_parse import MachO, rd
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from capstone.arm64 import ARM64_OP_REG, ARM64_OP_IMM, ARM64_OP_MEM

path = sys.argv[1]
m = MachO(rd(path))
t = m.text_section()
code = m.d[t['offset']:t['offset']+t['size']]
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True

symmap = {s['addr']: s['name'] for s in m.symbols if s['addr']}
sect = m.sections

def cstr(addr):
    for s in sect:
        if s['addr'] <= addr < s['addr'] + s['size'] and (addr - s['addr']) < s['size']:
            if s['name'] in ('__cstring', '__const', '__got', '__unwind_info'):
                fo = s['offset'] + (addr - s['addr'])
                if s['name'] in ('__cstring',):
                    end = m.d.find(b'\0', fo)
                    return 'cstr ' + repr(m.d[fo:end].decode('utf-8', 'replace'))
                if s['name'] == '__const':
                    val = struct.unpack_from('<Q', m.d, fo)[0]
                    inner = cstr(val) if val else ''
                    return f'const[{s["name"]}+0x{addr-s["addr"]:x}] -> {inner or hex(val)}'
    return ''

adrp = {}
lines = []
for ins in md.disasm(code, t['addr']):
    ann = ''
    mnem = ins.mnemonic
    ops = ins.operands
    if mnem == 'adrp' and len(ops) == 2 and ops[1].type == ARM64_OP_IMM:
        adrp[ops[0].reg] = ops[1].imm
    elif mnem in ('add', 'ldr', 'ldrb', 'ldrsb', 'str') and len(ops) >= 2:
        # add xN, xM, #imm  with xM tracked
        base = None
        if ops[1].type == ARM64_OP_REG and ops[1].reg in adrp:
            page = adrp[ops[1].reg]
            if len(ops) == 3 and ops[2].type == ARM64_OP_IMM:
                addr = page + ops[2].imm
                ann = f'; 0x{addr:x} {cstr(addr)}'
            elif len(ops) == 2:
                ann = f'; 0x{page:x} {cstr(page)}'
        elif ops[1].type == ARM64_OP_MEM and ops[1].mem.base in adrp:
            addr = adrp[ops[1].mem.base] + ops[1].mem.disp
            ann = f'; 0x{addr:x} {cstr(addr)}'
    if mnem == 'bl' and ops and ops[0].type == ARM64_OP_IMM:
        target = ops[0].imm
        ann = f'; -> {symmap.get(target, hex(target))}'
    if mnem == 'b' and ops and ops[0].type == ARM64_OP_IMM:
        target = ops[0].imm
        if target in symmap:
            ann = f'; -> {symmap[target]}'
    if mnem.startswith('b.') and ops and ops[0].type == ARM64_OP_IMM:
        pass
    off = ins.address
    mark = ''
    if off in symmap:
        mark = '\n' + '=' * 70 + f'\n{symmap[off]}:\n'
    lines.append(f"{mark}  {off:08x}  {mnem:8s} {ins.op_str:44s} {ann}")
print('\n'.join(lines))
