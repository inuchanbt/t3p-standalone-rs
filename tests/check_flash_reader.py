"""Host USB/reader checks, including actual C protocol responses; no device access."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('flash_read',ROOT/'flash_read.py')
host=importlib.util.module_from_spec(spec);spec.loader.exec_module(host)
fixture=b''

def seal(a):
    a[12:16]=bytes(4);struct.pack_into('<I',a,12,zlib.crc32(a));return bytes(a)

class Hid:
    def __init__(self):self.calls=0;self.q=None;self.bad=False
    def send_feature_report(self,q):
        assert len(q)==65 and q[0]==0 and q[1:5]==b'T3PQ'
        self.q=q[1:];self.calls+=1;return 65
    def get_feature_report(self,report_id,length):
        assert report_id==0 and length==65
        q=self.q;a=bytearray(fixture[:64]);a[24:]=bytes(40)
        a[8:12]=q[8:12];a[16:20]=q[12:16];a[6]=40
        if q[4]==0:a[24:]=fixture[24:64]
        else:
            offset=struct.unpack_from('<I',q,8)[0];a[6]=q[6]
            for i in range(q[6]):a[24+i]=(offset+i)&255
        if self.bad:a[24]^=1
        return b'\0'+seal(a)

class Checks(unittest.TestCase):
    def test_c_wire_responses(self):
        info=host.decode(fixture[:64],0,0);self.assertEqual(struct.unpack_from('<3I',info),(0xc000000,0x5000,0x1fa000))
        self.assertEqual(host.decode(fixture[64:],7,9),bytes(range(7,47)))
        for i in range(64):
            a=bytearray(fixture[64:]);a[i]^=1
            with self.assertRaises(ValueError):host.decode(a,7,9)
        with self.assertRaises(ValueError):host.decode(fixture[64:],8,9)
        with self.assertRaises(ValueError):host.decode(fixture[64:],7,10)
        for status in (1,2,3,4):
            a=bytearray(fixture[:64]);a[5]=status
            with self.assertRaises(ValueError):host.decode(seal(a),0,0)

    def test_double_acquisition_and_no_overwrite(self):
        d=Hid();r=host.Reader(d);info=r.info()
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temp:
            p=Path(temp)/'view.mapped.bin';result=host.save(r,p,7,93,info)
            self.assertEqual(p.read_bytes(),bytes(range(7,100)))
            self.assertEqual(d.calls,7) # info, three chunks, identical second three
            self.assertTrue(result['identical_second_acquisition'])
            self.assertEqual(json.loads(Path(str(p)+'.json').read_text())['bytes'],93)
            with self.assertRaises(FileExistsError):host.save(r,p,7,93,info)
            with self.assertRaises(ValueError):host.save(r,Path(temp)/'outside',info['view_bytes'],1,info)

    def test_changed_data_is_not_published(self):
        d=Hid();r=host.Reader(d);info=r.info();old=d.get_feature_report
        def changed(report_id,length):
            d.bad=d.calls>2
            return old(report_id,length)
        d.get_feature_report=changed
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temp:
            p=Path(temp)/'view.mapped.bin'
            with self.assertRaises(ValueError):host.save(r,p,0,20,info)
            self.assertFalse(p.exists());self.assertFalse(Path(str(p)+'.json').exists())
            self.assertTrue(Path(str(p)+'.partial').exists())

    def test_retry_after_corrupt_transfer(self):
        d=Hid();old=d.get_feature_report
        def damaged(report_id,length):
            a=bytearray(old(report_id,length))
            if d.calls==1:a[50]^=1
            return a
        d.get_feature_report=damaged
        self.assertEqual(host.Reader(d).info()['ready'],1);self.assertEqual(d.calls,2)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True);a=p.parse_args()
    out=ROOT/'build/usb-research';out.mkdir(parents=True,exist_ok=True)
    for name in ('flash_reader','flash_usb'):
        binary=out/(name+'_test.exe')
        subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror','-DT3P_BOOT_HOST',
                        str(ROOT/f'tests/{name}.c'),str(ROOT/'native/t3p_flash_reader.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)]+([str(out/'protocol-fixture.bin')] if name=='flash_reader' else []),check=True)
    global fixture
    fixture=(out/'protocol-fixture.bin').read_bytes()
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(Checks)
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():raise SystemExit(1)

if __name__=='__main__':main()
