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
    # Different controllers expose opposite bit-31 meanings. Check against
    # the pinned executable wait branch below, not a matching mock default.
    assert 'while(!(R(0xffe400)&0x80000000u))' in source
    assert 'while(R(SFC)&0x80000000u)' in source
    cache_header=(SDK/'interface/driver/cpu/br35/asm/cache.h').read_bytes()
    assert b'while(!(JL_DCU->CON & BIT(31)))' in cache_header
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
        (0xfb262,'2000','DcuWaitIdle synchronization'),
        (0xfb264,'c0ff00e4ff00','DCU CON register base'),
        (0xfb26a,'0160','read DCU CON'),
        (0xfb26c,'71fefdff','loop while signed CON > -1: bit 31 CLEAR'),
        (0xfb270,'8000','return only once bit 31 is SET (idle)'),
        (0xfb272,'f797','cache-wait trampoline to fb262'),
        (0xfb2da,'618b','sfc_suspend calls cache-wait trampoline fb272'),
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
    # Stock app executes these bytes from its startup-copied RAM area.
    # The SDK export-slot names describe its suspend/resume calls. The
    # reference ROM is separate evidence, not a dump of this device's ROM.
    app=(ROOT.parent/'doom_on_t3p/local/unpacked/T3P Firmware Version 3.0.3.bin/files/app.bin').read_bytes()
    assert hashlib.sha256(app).hexdigest()=='ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c'
    stock_calls=[(0x12128e,0xc163d2e,'4020','suspend argument 0'),
                 (0x121290,0xc163d30,'80ff26ebfdff','suspend export slot ffdbc'),
                 (0x1212aa,0xc163d4a,'4020','resume argument 0'),
                 (0x1212ac,0xc163d4c,'80ff0eebfdff','resume export slot ffdc0')]
    for runtime,mapped,raw,_ in stock_calls:
        b=bytes.fromhex(raw);assert app[mapped-0xc000100:mapped-0xc000100+len(b)]==b
        assert 0x120c60+mapped-0xc163700==runtime
    assert 0x121290+6-0x214da==0xffdbc and 0x1212ac+6-0x214f2==0xffdc0
    report=dict(status='OFFLINE SOURCE EVIDENCE; physical native SPI execution pending',
                sdk_cpu_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                reference_rom_sha256=hashlib.sha256(rom).hexdigest(),
                spi0_base='0xFE0700',portf_base='0xFE1000',sfc_base='0xFE0200',
                init_con0='0x125020',active_con0='0x125021',
                reference_instructions=[dict(address=hex(a),bytes=b,meaning=m) for a,b,m in evidence],
                dcu_idle_bit31=1,sfc_busy_bit31=1,
                sdk_cache_header_sha256=hashlib.sha256(cache_header).hexdigest(),
                stock_app_sha256=hashlib.sha256(app).hexdigest(),
                stock_call_instructions=[dict(runtime=hex(a),mapped=hex(m),bytes=b,meaning=s) for a,m,b,s in stock_calls],
                reference_rom_is_device_dump=False,
                sdk_objects_linked=False,rom_functions_called=False,device_access=False)
    (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Capture source: DCU idle polarity, 14 reference instructions and 4 stock RAM suspend/resume instructions verified')


if __name__=='__main__':main()
