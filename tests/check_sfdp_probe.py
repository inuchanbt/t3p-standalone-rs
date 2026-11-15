"""Read-only SFDP acquisition, protocol and evidence validation, offline only."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
import sfdp_read as host
from check_capture_device import fixture


def snapshot():
    # Constructed model only; table bytes come from the manufacturer transcript.
    data=bytearray(b'\xff'*4096)
    data[:24]=bytes.fromhex('53464450000101ff00000109300000ff85000103600000ff')
    data[0x30:0x54]=host.REFERENCE_BFPT
    return data


class FakeReader:
    def __init__(self):self.data=snapshot();self.calls=0;self.change=False
    def snapshot_info(self):return dict(bytes=4096,crc32=zlib.crc32(self.data),
        matching_spi_acquisitions=2,flash_mutation_code_excluded=True,jedec=0x856015)
    def info(self):return dict(jedec=0x856015,status=2)
    def diagnostics(self):return dict(step=9)
    def snapshot(self):
        self.calls+=1
        data=bytearray(self.data)
        if self.change and self.calls==2:data[0x38]^=1
        return bytes(data)


class Checks(unittest.TestCase):
    def test_all_tables_and_exact_difference(self):
        data=snapshot();result=host.tables(data)
        self.assertTrue(result['all_advertised_tables_acquired'])
        self.assertEqual([(t['parameter_id'],t['pointer'],t['dwords']) for t in result['tables']],
                         [('FF00',48,9),('FF85',96,3)])
        reference=result['tables'][0]['manufacturer_reference']
        self.assertTrue(reference['exact_match']);self.assertFalse(reference['differences'])
        data[0x38]^=1
        diff=host.tables(data)['tables'][0]['manufacturer_reference']['differences']
        self.assertEqual(diff,[dict(byte_offset=8,sfdp_address=56,expected=0x44,actual=0x45)])

    def test_truncated_header_table_and_missing_bfpt(self):
        for data in (snapshot()[:-1], bytes(4096)):
            with self.assertRaises(ValueError):host.tables(data)
        for at,value in ((19,0),(20,0),(21,255),(22,1),(12,0),(8,1)):
            data=snapshot();data[at]=value
            with self.assertRaises(ValueError):host.tables(data)

    def test_two_usb_reads_crc_and_no_overwrite(self):
        r=FakeReader();data,report=host.acquire(r)
        self.assertEqual(r.calls,2);self.assertTrue(report['identical_second_usb_acquisition'])
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
            path=Path(tmp)/'sfdp.bin';host.save(path,data,report)
            self.assertEqual(path.read_bytes(),data)
            self.assertEqual(json.loads(Path(str(path)+'.json').read_text())['snapshot']['matching_spi_acquisitions'],2)
            with self.assertRaises(FileExistsError):host.save(path,data,report)
        r=FakeReader();r.change=True
        with self.assertRaises(ValueError):host.acquire(r)
        r=FakeReader();r.snapshot_info=lambda:dict(bytes=0)
        with self.assertRaises(ValueError):host.acquire(r)
        r=FakeReader();original=r.snapshot_info
        r.snapshot_info=lambda:dict(original(),crc32=1)
        with self.assertRaises(ValueError):host.acquire(r)

    def test_manufacturer_reference_matches_c_fixture_and_guard(self):
        text=(ROOT/'tests/puya_p25q16h_sfdp.h').read_text()
        body=re.search(r'puya_bfpt\[\]=\{(.*?)\}',text,re.S)[1]
        self.assertEqual(bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]+)',body)),host.REFERENCE_BFPT)
        header=re.search(r'puya_header\[\]=\{(.*?)\}',text,re.S)[1]
        self.assertEqual(bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]+)',header)),host.REFERENCE_HEADER)
        text=(ROOT/'native/t3p_capture_store.c').read_text()
        body=re.search(r'expected\[\]=\{(.*?)\}',text,re.S)[1]
        self.assertEqual(bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]+)',body)),host.REFERENCE_BFPT)

    def test_metadata_and_chunk_protocol(self):
        r=host.SfdpReader(None);calls=[];data=snapshot()
        def exchange(op,at=0,length=0):
            calls.append((op,at,length))
            if op==4:return struct.pack('<10I',0x31444653,4096,zlib.crc32(data),2,1,0x856015,0,0,0,0)
            self.assertEqual(op,3);self.assertTrue(1<=length<=40);return bytes(data[at:at+length])
        r.exchange=exchange
        self.assertEqual(r.snapshot_info()['matching_spi_acquisitions'],2)
        self.assertEqual(r.snapshot(),data);self.assertEqual(calls[-1],(3,4080,16))
        for index,value in ((0,0),(1,1),(3,1),(4,0),(6,1)):
            fields=[0x31444653,4096,0,2,1,0x856015,0,0,0,0];fields[index]=value
            r.exchange=lambda *args:struct.pack('<10I',*fields)
            with self.assertRaises(ValueError):r.snapshot_info()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True)
    a=p.parse_args();out=ROOT/'build/capture-sfdp-probe';out.mkdir(parents=True,exist_ok=True)
    plain,_=fixture();f=out/'layout-fixture.bin';f.write_bytes(plain[0x5000:0x5100]+plain[0x85000:0x85020])
    for name in ('sfdp_store','sfdp_usb','flash_usb'):
        binary=out/(name+'-test.exe');flags=[];sources=[ROOT/f'tests/{name}.c']
        if name=='flash_usb':
            flags=['-DT3P_CAPTURE_SFDP_PROBE','-DT3P_CAPTURE_DEVICE','-DT3P_BOOT_HOST']
            sources.append(ROOT/'native/t3p_capture_usb.c')
        subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror',*flags,*map(str,sources),'-o',str(binary)],check=True)
        subprocess.run([str(binary),*([str(f)] if name=='sfdp_store' else [])],check=True)
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checks))
    if not result.wasSuccessful():raise SystemExit(1)
    print('SFDP probe checks passed; fixtures are synthetic, not acquired device evidence.')


if __name__=='__main__':main()
