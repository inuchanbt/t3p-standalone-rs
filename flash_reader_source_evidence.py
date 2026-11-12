"""Pin the stock USB register sequence used by the reader trial (offline only)."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parent
def main():
    base=ROOT.parent/'doom_on_t3p'
    boot=(base/'local/unpacked/T3P Firmware Version 3.0.3.bin/top/uboot.boot').read_bytes()
    cpu=(base/'local/jieli-sdk/SDK/cpu/br35/liba/cpu.a').read_bytes()
    assert hashlib.sha256(boot).hexdigest()=='53b7b7f6ec1711becb538625a5afe4b45cc3dba5fafee8bcb229bd12d0791ec1'
    assert hashlib.sha256(cpu).hexdigest()=='787b6fc0913a0a8a634ee8c7606d5e4a68dc5604d8feacf806fd60dc1e2fc8ca'
    evidence=[(0x104320,'c0 ff 0c 35 fd 00','USB port direction base'),
              (0x104326,'00 ef 02 00','DM direction input'),
              (0x10432a,'00 ef 01 00','DP direction input'),
              (0x10432e,'c9 ef 80 0f','clear port IO mode'),
              (0x104332,'c9 ef 04 00','clear port SR0'),
              (0x104336,'41 e0 8b bf','USB CON0 clear mask'),
              (0x104346,'09 ef 01 00','enable USB receiver'),
              (0x10434a,'c4 ef 03 00','clear DP/DM pulldown'),
              (0x10434e,'c2 ef 03 00','clear DP/DM pullup'),
              (0x104352,'02 ef 01 00','enable DP pullup'),
              (0x104356,'00 ef 3c 50','USB reset/PHY bits'),
              (0x104372,'00 ef 00 5e','enable USB CON0.SIEIE peripheral event gate'),
              (0x104ec2,'c5 ff 00 20 fd 00','SIE register base'),
              (0x104f04,'50 e8 1a 10','USB reset interrupt bit'),
              (0x104f18,'40 20','reset address to zero'),
              (0x104f1c,'d0 60','write USB address')]
    for address,raw,_ in evidence:
        b=bytes.fromhex(raw);at=16+address-0x102600
        assert boot[at:at+len(b)]==b,hex(address) # 16-byte loader file header
    app=(base/'local/unpacked/T3P Firmware Version 3.0.3.bin/files/app.bin').read_bytes()
    assert hashlib.sha256(app).hexdigest()=='ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c'
    tx_evidence=[(0xc0cb1fc,'40 ea 40 20','service RX before entering the TX helper'),
                 (0xc0cb206,'b6 88','branch to TX dispatch after RX service'),
                 (0xc0cacda,'bf ea 2c ff','call TX helper'),
                 (0xc0cab38,'c6 ff 44 20 fd 00','TX helper CSR0 base'),
                 (0xc0cab42,'51 e8 30 0a','defer while TX ready is set'),
                 (0xc0cab80,'f5 62','arm EP0 byte count after payload copy'),
                 (0xc0cab9e,'40 ea 0a 60','start final TX packet separately from RX service')]
    for address,raw,_ in tx_evidence:
        b=bytes.fromhex(raw);at=address-0xc000100
        assert app[at:at+len(b)]==b,hex(address)
    assert 0xc0cb206+2-0x530==0xc0cacd8
    assert 0xc0cacda+4-0x1a8==0xc0cab36
    report=dict(status='OFFLINE SOURCE EVIDENCE; reader USB enumeration and CPU mapping reads NOT device-verified',
                stock_boot_sha256=hashlib.sha256(boot).hexdigest(),
                sdk_cpu_sha256=hashlib.sha256(cpu).hexdigest(),
                instructions=[dict(address=hex(a),bytes=b,meaning=m) for a,b,m in evidence],
                stock_app_sha256=hashlib.sha256(app).hexdigest(),
                tx_order_instructions=[dict(address=hex(a),bytes=b,meaning=m) for a,b,m in tx_evidence],
                sdk_research_members=['usb.c.o','usb_phy.c.o','usb_setup.c.o'],sdk_objects_linked=False,
                flash_program_erase_interface=False,raw_spi_commands=False,
                usb_irq_mode='polling, CPU IRQs remain disabled',
                inherited_clock_configuration=True)
    out=ROOT/'build/flash-reader/source-evidence.json';out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Verified {len(evidence)} boot USB and {len(tx_evidence)} app TX instructions plus dispatch targets; no device access')

if __name__=='__main__':main()
