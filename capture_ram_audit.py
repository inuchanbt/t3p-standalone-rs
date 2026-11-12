"""Reject flash references from the SRAM SPI execution island."""
import struct


def elf_tables(path):
    data=path.read_bytes();shoff=struct.unpack_from('<I',data,32)[0]
    size,count,names=struct.unpack_from('<3H',data,46)
    rows=[struct.unpack_from('<10I',data,shoff+i*size) for i in range(count)]
    payload=lambda r:data[r[4]:r[4]+r[5]]
    string=lambda table,at:table[at:].split(b'\0',1)[0].decode('ascii')
    labels=[string(payload(rows[names]),r[0]) for r in rows]
    symbols=[]
    for r in rows:
        if r[1]!=2:continue
        strings=payload(rows[r[6]])
        for at in range(r[4],r[4]+r[5],r[9]):
            name,value,n,info,_,index=struct.unpack_from('<IIIBBH',data,at)
            symbols.append((string(strings,name),value,n,info,index))
    return data,rows,labels,symbols


def inspect(obj,elf):
    data,rows,labels,symbols=elf_tables(obj)
    island=labels.index('.data.flash_ram')
    checked=0
    for r in rows:
        if r[1] not in (4,9) or r[7]!=island:continue
        for at in range(r[4],r[4]+r[5],r[9]):
            _,info=struct.unpack_from('<II',data,at)
            target=symbols[info>>8];index=target[4]
            if index==0 or index>=len(labels) or not labels[index].startswith(('.data','.bss')):
                raise ValueError(f'SPI SRAM island references non-RAM symbol {target[0]}')
            checked+=1
    if not checked:raise ValueError('No SRAM relocations audited')
    _,_,_,linked=elf_tables(elf)
    functions={s[0]:s[1] for s in linked if s[3]&15==2}
    names=('identify','enter','leave','byte','cs','address','status','read_raw','operate','clock_tick')
    for name in names:
        if not 0x101a80<=functions.get(name,0)<0x132e00:
            raise ValueError(f'SPI function {name} is not in owned SRAM')
    return dict(ram_functions_checked=len(names),ram_only_relocations_checked=checked,
                rom_calls=False,flash_constants_reachable_while_suspended=False)
