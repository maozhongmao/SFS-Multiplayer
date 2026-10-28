import sys, json
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from elftools.elf.relocation import RelocationSection

def load(p):
    f = open(p, 'rb'); return f, ELFFile(f)

def info(p):
    f, e = load(p)
    d = {}
    d['path'] = p
    d['class'] = e.elfclass
    d['machine'] = e['e_machine']
    d['type'] = e['e_type']
    d['entry'] = hex(e['e_entry'])
    d['sections'] = []
    for s in e.iter_sections():
        if s['sh_size']:
            d['sections'].append(dict(name=s.name, addr=hex(s['sh_addr']), off=hex(s['sh_offset']),
                                      size=hex(s['sh_size']), flags=int(s['sh_flags']), type=s['sh_type']))
    dyn = e.get_section_by_name('.dynamic')
    d['needed'] = []
    if dyn:
        for t in dyn.iter_tags():
            if t.entry.d_tag == 'DT_NEEDED':
                d['needed'].append(t.needed)
            if t.entry.d_tag in ('DT_SONAME',):
                d['soname'] = t.soname
            if t.entry.d_tag == 'DT_INIT':
                d['init'] = hex(t.entry.d_val)
            if t.entry.d_tag == 'DT_INIT_ARRAY':
                d['init_array'] = hex(t.entry.d_val)
    syms = []
    for s in e.iter_sections():
        if isinstance(s, SymbolTableSection):
            for sym in s.iter_symbols():
                if sym['st_value'] and sym['st_shndx'] != 'SHN_UNDEF':
                    syms.append(dict(name=sym.name, value=sym['st_value'], size=sym['st_size'],
                                     bind=sym['st_info']['bind'], typ=sym['st_info']['type'],
                                     vis=sym['st_other']['visibility'], sect=sym['st_shndx']))
    d['symbols'] = syms
    f.close()
    return d

if __name__ == '__main__':
    d = info(sys.argv[1])
    print(json.dumps({k: v for k, v in d.items() if k != 'symbols'}, indent=1))
    print('SYMCOUNT', len(d['symbols']))
    if len(sys.argv) > 2 and sys.argv[2] == 'syms':
        for s in sorted(d['symbols'], key=lambda x: x['value']):
            print(hex(s['value']), s['size'], s['bind'], s['typ'], s['sect'], s['name'])
