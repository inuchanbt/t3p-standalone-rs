"""Execute linked capture qualification/diagnostics on Pi32v2 with synthetic reads.

The profile cases hook read-only SPI transaction boundaries. Separate SRAM
entry cases execute the actual linked enter/leave functions against independent
MMIO readiness values. Neither model establishes real peripheral timing.
"""
import argparse
import builtins
import hashlib
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
    p.add_argument('--sfdp-probe',action='store_true',help='Read-only SFDP acquisition variant')
    p.add_argument('--capture-ens220',action='store_true',help='Require normal V6 build without comparison probes')
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
            if getattr(self,'controller_probe',False) and address==0xffe400 and count==4:
                self.dcu_reads+=1
                idle=self.pending is not None and self.dcu_reads>self.pending
                self.idle=idle
                return struct.pack('<I',(0x80000000 if idle else 0)|self.enabled)
            if 0x04000100<=address<0x04165e40:address+=0x08000000
            return super().read(address,count)
        def write(self,address,data):
            if getattr(self,'controller_probe',False) and 0xfe0200<=address<0xfe1120:
                assert self.idle, 'Controller changed before DCU idle'
                self.controller_writes.append((address,int.from_bytes(data,'little')))
            return super().write(address,data)
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
    device_table=(ROOT/'tests/t3p_856015_sfdp.h').read_text()
    body=re.search(r't3p_856015_bfpt\[\]=\{([^}]+)',device_table)[1]
    device_bfpt=bytes(int(n,16) for n in re.findall(r'0x([0-9a-f]+)',body))
    observed=bytearray(legacy);observed[0x30:0x54]=device_bfpt
    cases.append(('acquired T3P legacy table',observed,0x856015,0,0))
    mixed=bytearray(legacy);mixed[0x32]=0xf9
    cases.append(('unobserved mixed table',mixed,0x856015,2,9))
    cases.append(('wrong chip ID',legacy,0xef4015,2,5))
    cases.append(('zero SFDP',bytearray(128),0x856015,2,4))
    bad=bytearray(legacy);bad[0x38]^=1
    cases.append(('unqualified legacy content',bad,0x856015,2,9))
    bad=bytearray(legacy);bad[0x34]^=1
    cases.append(('wrong density',bad,0x856015,2,7))
    modern=bytearray(legacy);modern[11]=11;modern[0x58]=0x80
    cases.append(('modern page-size word',modern,0xef4015,0,0))
    if a.sfdp_probe:
        cases=[(name,data+bytes([255])*(4096-len(data)),jedec,
                status or 2,step or 12) for name,data,jedec,status,step in cases]
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
                assert kind==1 and n in ((24,36,44,256,4096) if a.sfdp_probe else (24,36,44)) and at+n<=len(sfdp)
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
        if a.sfdp_probe:
            # Both new operations serve only the cached snapshot. They never
            # perform SPI, and remain available with an unqualified profile.
            q[4]=4;cpu.mem.write(request,q)
            cpu.call(symbols['t3p_capture_usb_request'],[request,answer])
            meta=struct.unpack('<10I',decode(cpu.mem.read(answer,64),0,73,b'T3CR',2))
            valid=sfdp[:4]==b'SFDP'
            assert meta[0]==0x31444653 and meta[1]==(4096 if valid else 0) and meta[4]==1
            assert meta[3]==(2 if valid else 0) and meta[5]==jedec
            if valid:
                q[4]=3;q[6]=40;struct.pack_into('<I',q,8,48);cpu.mem.write(request,q)
                cpu.call(symbols['t3p_capture_usb_request'],[request,answer])
                assert decode(cpu.mem.read(answer,64),48,73,b'T3CR',2)==sfdp[48:88]
            assert transactions==before
            # Execute the actual linked refusal callbacks, including a forged
            # ready status; no SPI operation may be reached.
            cpu.mem.write(symbols['t3p_capture_status'],bytes(4))
            for fn,args in (('t3p_capture_store_prepare',[]),('t3p_capture_store_program',[0,request,1]),
                            ('t3p_capture_store_flush',[])):
                assert cpu.call(symbols[fn],args)[0]==0xffffffff
            assert transactions==before
        results.append(dict(case=name,status=status,step=step,synthetic_read_transactions=transactions))
    enter_results=[]
    rom=(ROOT.parent/'doom_on_t3p/local/jieli-sdk/SDK/cpu/br35/tools/rom.image').read_bytes()
    assert hashlib.sha256(rom).hexdigest()=='12341f7db1d764b865466c8d54447b53d8faabe9867ec728752d44085dd0c8f7'
    # Execute the pinned primary wait instructions in the simulator only.
    # These addresses are never used as runtime ROM calls by the firmware.
    cpu.mem.write(0xfb262,rom[0x3262:0x3272])
    for name,pending,enabled in (('already idle',0,1),('pending then idle',7,1),
                                 ('disabled and idle',0,0),('never idle',None,1)):
        cpu.mem.controller_probe=False
        symbols=cpu.load_elf(elf);cpu.sp=symbols['__runtime_stack_end']
        initial={0xfe0200:0x40000003,0xfe0700:0x1000,0xfe0704:7,
                 0xfe0714:4,0xfe1004:0xa5,0xfe1008:0xffffffff}
        for at,v in initial.items():cpu.mem.write(at,struct.pack('<I',v))
        cpu.mem.write(symbols['cs_mask'],struct.pack('<I',1))
        cpu.mem.pending=pending;cpu.mem.enabled=enabled;cpu.mem.dcu_reads=0
        cpu.mem.controller_writes=[];cpu.mem.idle=False;cpu.mem.controller_probe=True
        try:
            if pending is not None:
                cpu.call(0xfb262,[])
                assert cpu.mem.dcu_reads==pending+1 and not cpu.mem.controller_writes
                cpu.mem.dcu_reads=0;cpu.mem.idle=False
            result=cpu.call(symbols['enter'],[],limit=2_000_000)[0]
            if pending is None:
                assert result==0xffffffff and not cpu.mem.controller_writes
                assert int.from_bytes(cpu.mem.read(symbols['t3p_capture_diagnostics']+4,4),'little')==1
            else:
                assert result==0 and cpu.mem.dcu_reads==pending+1
                assert int.from_bytes(cpu.mem.read(0xfe0200,4),'little')&3==0
                assert int.from_bytes(cpu.mem.read(0xfe0700,4),'little')==0x125021
                cpu.call(symbols['leave'],[])
            for at,v in initial.items():assert int.from_bytes(cpu.mem.read(at,4),'little')==v
            enter_results.append(dict(case=name,dcu_polls=cpu.mem.dcu_reads,
                                      controller_writes=len(cpu.mem.controller_writes),result=result))
            print(f'Pi32v2 SRAM entry: {name} passed',flush=True)
        finally:cpu.mem.controller_probe=False
    temperature_probe_cases=None
    if 't3p_temperature_probe_page' in symbols:
        # Exercise the linked finite-probe cache/USB implementation with a
        # synthetic C fixture. No GPIO/SPI MMIO is mapped for these calls.
        from temperature_read import decode_probes
        wire=(out/'probe-usb-check.frames').read_bytes()
        assert len(wire)==98*64
        fixture_pages=[decode(wire[i*64:(i+1)*64],i,123,b'T3CR',2) for i in range(98)]
        expected=b''.join(fixture_pages[:97])
        symbols=cpu.load_elf(elf);cpu.sp=symbols['__runtime_stack_end']
        request=symbols['__runtime_heap_start'];answer=request+64
        cpu.call(symbols['t3p_temperature_probe_init'],[1])
        assert cpu.call(symbols['t3p_temperature_probe_page'],[1,answer])[0]==0xffffffff
        cpu.mem.write(symbols['live'],expected)
        original_read,original_write=cpu.mem.read,cpu.mem.write
        def cache_read(at,count):
            assert not (0xfd0000<=at<0x1000000),'Probe USB tried hardware MMIO'
            return original_read(at,count)
        def cache_write(at,data):
            assert not (0xfd0000<=at<0x1000000),'Probe USB tried hardware MMIO'
            return original_write(at,data)
        cpu.mem.read, cpu.mem.write=cache_read,cache_write
        pages=[]
        try:
            for page in range(98):
                q=bytearray(64);q[:5]=b'T3CQ\x05';struct.pack_into('<II',q,8,page,123)
                cpu.mem.write(request,q)
                cpu.call(symbols['t3p_capture_usb_request'],[request,answer])
                pages.append(decode(cpu.mem.read(answer,64),page,123,b'T3CR',2))
                if page==0:
                    # Changing the live count cannot refresh a frozen record.
                    cpu.mem.write(symbols['live']+5*4,bytes(4))
            actual=b''.join(pages[:97]);assert actual==expected
            decoded=decode_probes(actual,pages[97]);assert decoded['completed']==8
            q[6]=1;cpu.mem.write(request,q)
            cpu.call(symbols['t3p_capture_usb_request'],[request,answer])
            assert cpu.mem.read(answer+5,1)==b'\x01'
            temperature_probe_cases=dict(cached_pages=98,GPIO_SPI_MMIO_accesses=0,
                live_change_did_not_refresh=True,decoded_records=8,synthetic_only=True)
            print('Pi32v2 temperature probe: 98 cached USB pages, frozen CRC and 8 synthetic records match; no GPIO/SPI MMIO',flush=True)
        finally:cpu.mem.read,cpu.mem.write=original_read,original_write
    ens220_cases=None
    if 't3p_ens220_centidegrees' in symbols:
        from temperature_read import decode as decode_temperature
        symbols=cpu.load_elf(elf);cpu.sp=symbols['__runtime_stack_end']
        request=symbols['__runtime_heap_start'];answer=request+64
        for raw,expected_value in ((0x9513,2500),(34963,0),(0x8000,-1715),(29843,-4000),(45843,8500)):
            assert cpu.call(symbols['t3p_ens220_centidegrees'],[raw,answer])[0]==0
            assert int.from_bytes(cpu.mem.read(answer,4),'little',signed=True)==expected_value
        for raw in (0,0xffff,0x10000,29842,45844):
            assert cpu.call(symbols['t3p_ens220_centidegrees'],[raw,answer])[0]==0xffffffff
        fixture=[int(x,16) for x in (ROOT/'tests/ens220_diag.words').read_text().split()]
        expected=struct.pack('<110I',*fixture)
        cpu.mem.write(symbols['t3p_temperature_diagnostics'],expected)
        original_read,original_write=cpu.mem.read,cpu.mem.write
        def cached_temperature_read(at,count):
            assert not (0xfd0000<=at<0x1000000),'Temperature USB tried hardware MMIO'
            return original_read(at,count)
        def cached_temperature_write(at,data):
            assert not (0xfd0000<=at<0x1000000),'Temperature USB tried hardware MMIO'
            return original_write(at,data)
        cpu.mem.read,cpu.mem.write=cached_temperature_read,cached_temperature_write
        pages=[]
        try:
            for page in range(12):
                q=bytearray(64);q[:5]=b'T3CQ\x04';struct.pack_into('<II',q,8,page,79)
                cpu.mem.write(request,q);cpu.call(symbols['t3p_capture_usb_request'],[request,answer])
                pages.append(decode(cpu.mem.read(answer,64),page,79,b'T3CR',2))
                if page==0:cpu.mem.write(symbols['t3p_temperature_diagnostics']+4,bytes(4))
            actual=b''.join(pages[:11]);assert actual==expected
            result=decode_temperature(actual,pages[11])
            assert result['sensor']=='ENS220' and result['temperature_C']==25 and result['ID']=='0321'
            ens220_cases=dict(conversion_cases=10,cached_pages=12,GPIO_SPI_MMIO_accesses=0,synthetic_only=True)
            print('Pi32v2 ENS220: ten conversion cases and 12 frozen V3 USB pages match; no GPIO/SPI MMIO',flush=True)
        finally:cpu.mem.read,cpu.mem.write=original_read,original_write
    normal_capture=None
    if a.capture_ens220:
        assert not any(name.startswith('t3p_temperature_probe_') for name in symbols),'Comparison probes linked into normal V6'
        assert 't3p_ens220_poll' in symbols and ens220_cases is not None
        symbols=cpu.load_elf(elf);cpu.sp=symbols['__runtime_stack_end']
        request=symbols['__runtime_heap_start'];answer=request+64
        original_read,original_write=cpu.mem.read,cpu.mem.write
        def normal_read(at,count):
            assert not (0xfd0000<=at<0x1000000),'Rejected probe request reached MMIO'
            return original_read(at,count)
        def normal_write(at,data):
            assert not (0xfd0000<=at<0x1000000),'Rejected probe request reached MMIO'
            return original_write(at,data)
        cpu.mem.read,cpu.mem.write=normal_read,normal_write
        try:
            q=bytearray(64);q[:5]=b'T3CQ\x05';struct.pack_into('<II',q,8,0,81)
            cpu.mem.write(request,q);cpu.call(symbols['t3p_capture_usb_request'],[request,answer])
            reply=cpu.mem.read(answer,64);assert reply[:4]==b'T3CR' and reply[5]==1 and reply[6]==0
            normal_capture=dict(comparison_probe_symbols=0,operation_5_rejected=True,GPIO_SPI_MMIO_accesses=0)
            print('Pi32v2 normal V6: comparison probe code absent; operation 5 rejected without MMIO',flush=True)
        finally:cpu.mem.read,cpu.mem.write=original_read,original_write
    (out/'OFFLINE-probe-pi32.json').write_text(json.dumps(dict(
        format='SYNTHETIC READ/MMIO; not hardware SPI',cases=results,
        sram_entry_cases=enter_results,temperature_probe_cache=temperature_probe_cases,ens220=ens220_cases,normal_capture=normal_capture),indent=2)+'\n')
    print(f'Pi32v2 capture: {len(cases)} compiled profile/USB cases and 4 actual SRAM entry cases passed')


if __name__=='__main__':main()
