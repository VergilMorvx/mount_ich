import sys
sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from macho_parse import MachO, rd
m = MachO(rd(sys.argv[1]))
s = next(x for x in m.sections if x['name'] == '__cstring')
blob = m.d[s['offset']:s['offset']+s['size']]
i = 0
while i < len(blob):
    e = blob.find(b'\0', i)
    if e < 0:
        break
    txt = blob[i:e].decode('utf-8', 'replace')
    print("0x%08x %r" % (s['addr'] + i, txt))
    i = e + 1
