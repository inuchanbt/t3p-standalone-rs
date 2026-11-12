"""Execute linked capture qualification/diagnostics on Pi32v2 with synthetic reads.

Hooks only the read-only SPI transaction boundaries. This checks the target ABI,
compiled layout/profile guards and USB diagnostics, NOT real peripheral timing.
"""
import argparse
import builtins
from functools import lru_cache
from importlib import import_module
from io import StringIO
import json
from pathlib import Path
import re
import struct
import sys
from unittest.mock import patch

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
from flash_read import decode


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/capture-device')
    a=p.parse_args();out=a.build_dir
    sim=ROOT/'local/pi32v2-isa-66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f'
    sys.path.insert(0,str(sim/'tools/llvm'))
    from sim import CPU,Memory
    import check
    check.takes_state=lru_cache(maxsize=None)(check.takes_state)
    ids={c['id'] for n in ('followup5-m.json','followup5-m-main.json')
         for c in json.loads((sim/'hw'/n).read_bytes())}
    ids.update(json.loads(r)['id'] for r in (sim/'hw/fm1-2026-10-24-v2/results.jsonl').read_bytes().splitlines())
    for mod in (sim/'tools/hwtest/semantics').glob('*.py'):
        if not mod.name.startswith('_') and mod.stem!='m2':
            ids.difference_update(import_module(f'semantics.{mod.stem}').MODELS)
    real_open=builtins.open
    def model_ids(path,*args,**kwargs):
        if path=='/root/.t3/projects/pi32v2-isa-wt/ids5-m.txt':return StringIO('\n'.join(sorted(ids)))
        return real_open(path,*args,**kwargs)
    with patch('builtins.open',model_ids):cpu=CPU()
    class FlashMemory(Memory):
        def read(self,address,count):
            if 0x04000100<=address<0x04165e40:address+=0x08000000
            return super().read(address,count)
    cpu.mem=FlashMemory()
    elf=(out/'t3p-meter-demo.elf').read_bytes()
    fixture=(out/'layout-fixture.bin').read_bytes()
    tables=(ROOT/'tests/puya_p25q16h_sfdp.h').read_text()
    def table(name):
        body=re.search(name+r'\[\]=\{([^}]+)',tables)[1]
        return bytes(int(n,16) for n in re.findall(r'0x([0-9a-f]+)',body))
    legacy=bytearray(b'\xff'*128)
    legacy[:24]=table('puya_header');legacy[0x30:0x54]=table('puya_bfpt')
    cases=[]
    cases.append(('manufacturer legacy table',legacy,0x856015,0,0))
    cases.append(('wrong chip ID',legacy,0xef4015,2,5))
    cases.append(('zero SFDP',bytearray(128),0x856015,2,4))
    bad=bytearray(legacy);bad[0x38]^=1
    cases.append(('unqualified legacy content',bad,0x856015,2,9))
    bad=bytearray(legacy);bad[0x34]^=1
    cases.append(('wrong density',bad,0x856015,2,7))
    modern=bytearray(legacy);modern[11]=11;modern[0x58]=0x80
    cases.append(('modern page-size word',modern,0xef4015,0,0))
    results=[]
    for name,sfdp,jedec,status,step in cases:
        symbols=cpu.load_elf(elf);cpu.sp=symbols['__runtime_stack_end']
        cpu.mem.write(0xc000000,fixture[:256]);cpu.mem.write(0xc080000,fixture[256:])
        # Occupied/unknown slots stop the virgin scan immediately; no erase
        # is requested or emulated by this initialization test.
        for slot in range(6):cpu.mem.write(0xc08b000+slot*0x20000,b'\0')
        for at,value in ((0xfe0210,0xf4005000),(0xfe0204,0x18),(0xfe0224,0),(0xfe1104,0)):
            cpu.mem.write(at,struct.pack('<I',value))
        old=cpu.step;transactions=[]
        def read_step():
            pc=cpu.pc+0x08000000 if 0x04000100<=cpu.pc<0x04165e40 else cpu.pc
            if pc==symbols['identify']:
                cpu.mem.write(cpu.R[0],jedec.to_bytes(3,'big'))
            elif pc==symbols['read_raw']:
                at,dest,n,kind=cpu.R[:4]
                assert kind==1 and n in (24,36,44) and at+n<=len(sfdp)
                transactions.append((at,n));cpu.mem.write(dest,sfdp[at:at+n])
            else:return old()
            cpu.R[0]=0;cpu.pc=cpu.rets
        cpu.step=read_step
        try:r=cpu.call(symbols['t3p_capture_store_init'],[0])[0]
        finally:cpu.step=old
        actual=int.from_bytes(cpu.mem.read(symbols['t3p_capture_status'],4),'little')
        words=cpu.mem.read(symbols['t3p_capture_diagnostics'],40)
        assert (actual,struct.unpack_from('<I',words)[0],r)==(status,step,0 if not status else 0xffffffff),(name,r,actual,words.hex())
        request=symbols['__runtime_heap_start'];answer=request+64
        q=bytearray(64);q[:5]=b'T3CQ\x02';struct.pack_into('<I',q,12,73)
        cpu.mem.write(request,q);before=list(transactions)
        cpu.call(symbols['t3p_capture_usb_request'],[request,answer])
        assert decode(cpu.mem.read(answer,64),0,73,b'T3CR',2)==words
        assert transactions==before
        results.append(dict(case=name,status=status,step=step,synthetic_read_transactions=transactions))
    (out/'OFFLINE-probe-pi32.json').write_text(json.dumps(dict(
        format='SYNTHETIC READ BOUNDARIES; not hardware SPI',cases=results),indent=2)+'\n')
    print('Pi32v2 compiled capture qualification and cached USB diagnostics: 6 cases passed')


if __name__=='__main__':main()
