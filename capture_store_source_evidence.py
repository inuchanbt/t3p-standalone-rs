"""Cross-check capture MMIO constants and sequences against pinned source evidence.

Reads local SDK/reference ROM files only. Does not open a USB device, use ROM
entry points, or link any SDK object into firmware.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parent
SDK=ROOT.parent/'doom_on_t3p/local/jieli-sdk/SDK'


def main():
    archive=SDK/'cpu/br35/liba/cpu.a'
    assert hashlib.sha256(archive.read_bytes()).hexdigest()=='787b6fc0913a0a8a634ee8c7606d5e4a68dc5604d8feacf806fd60dc1e2fc8ca'
    header=(SDK/'interface/driver/cpu/br35/asm/br35.h').read_text(encoding='utf-8')
    assert re.search(r'JL_SPI0_BASE\s+\(hs_base \+ map_adr\(0x07, 0x00\)\)',header)
    assert re.search(r'JL_PORTF_BASE\s+\(hs_base \+ map_adr\(0x10, 0x00\)\)',header)
    source=(ROOT/'native/t3p_capture_store.c').read_text(encoding='utf-8')
    for name,value in (('SPI',0xfe0700),('GPIO',0xfe1000),('SFC',0xfe0200)):
        actual=int(re.search(r'^#define '+name+r' (0x[0-9a-f]+)u',source,re.M)[1],16)
        assert actual==value,(name,hex(actual))
    out=ROOT/'build/capture-device/source-evidence';out.mkdir(parents=True,exist_ok=True)
    ar=ROOT/'local/pi32v2-llvm-23.1.5-2-windows-x64/bin/llvm-ar.exe'
    obj=out/'tzspi.c.o';obj.write_bytes(subprocess.run([ar,'p',archive,'tzspi.c.o'],check=True,capture_output=True).stdout)
    clang=ROOT.parent/'doom_on_t3p/local/tools/jieli/C$/JL/pi32/bin/clang.exe'
    ir=out/'tzspi.ll'
    subprocess.run([clang,'-target','pi32v2','-mcpu=r3','-S','-emit-llvm','-x','ir',obj,'-o',ir],check=True)
    text=ir.read_text(encoding='utf-8')
    facts=('store volatile i32 1200160, i32* inttoptr (i32 16647936',
           'i32 16647940 to i32*','i32 16647944 to i32*','i32 16647956 to i32*',
           'i32 16650244 to i32*','i32 16650248 to i32*')
    assert all(f in text for f in facts)
    rom=(SDK/'cpu/br35/tools/rom.image').read_bytes()
    assert hashlib.sha256(rom).hexdigest()=='12341f7db1d764b865466c8d54447b53d8faabe9867ec728752d44085dd0c8f7'
    evidence=[
        (0xfb2dc,'c0ff0002fe00','SFC register base'),
        (0xfb2e8,'c0ef0100','clear SFC enable bit 0'),
        (0xfb2ec,'c0ef0200','clear SFC bit 1'),
        (0xfb2fe,'c0ff0007fe00','SPI0 base, not a separate clock register'),
        (0xfb2f6,'00ef0100','enable SPI0 CON bit 0 after init configuration'),
        (0xfb30e,'00ef0200','restore SFC bit 1'),
        (0xfb312,'00ef0100','restore SFC bit 0'),
    ]
    for at,raw,_ in evidence:
        assert rom[at-0xf8000:at-0xf8000+len(bytes.fromhex(raw))]==bytes.fromhex(raw)
    report=dict(status='OFFLINE SOURCE EVIDENCE; physical native SPI execution pending',
                sdk_cpu_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                reference_rom_sha256=hashlib.sha256(rom).hexdigest(),
                spi0_base='0xFE0700',portf_base='0xFE1000',sfc_base='0xFE0200',
                init_con0='0x125020',active_con0='0x125021',
                reference_instructions=[dict(address=hex(a),bytes=b,meaning=m) for a,b,m in evidence],
                sdk_objects_linked=False,rom_functions_called=False,device_access=False)
    (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Capture source: pinned SPI0/PORTF constants, SPI byte registers and 7 SFC/SPI reference instructions verified')


if __name__=='__main__':main()
