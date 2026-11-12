"""Offline capture partition, physical NOR model and USB/BMP integration checks."""
import argparse
from binascii import crc_hqx
from io import BytesIO
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
import capture_layout as layout
import capture_export as host
from flash_read import decode
from PIL import Image


def fixture():
    p=bytearray(b'\xff'*0x178000)
    p[0x5000:0x5020]=layout.header(b'app_area_head',0xc000100,0x165e40,0x83)
    p[0x5020:0x5040]=layout.header(b'app.bin',0x100,0x165d40,0x82)
    payload=bytes(p[0x5100:layout.OLD_END])
    encoded,plain,bounds=layout.repack(p,p,0,payload,lambda *args:None)
    return plain,bounds


def store():
    data=struct.pack('<HH',0x8000|1920,0x7e0)*30
    h=bytearray(b'\xff'*64);h[:32]=bytes(32);h[:8]=b'T3PSCR01';h[8:10]=b'\x01\0';h[10:14]=b'SCRN'
    struct.pack_into('<II',h,32,len(data),zlib.crc32(data));struct.pack_into('<I',h,40,zlib.crc32(h[:40]));h[60:]=b'DONE'
    s=bytearray(b'\xff'*(6*host.SLOT));s[:64]=h;s[64:64+len(data)]=data
    return s


class FakeReader:
    def __init__(self,s):self.s=s;self.calls=0;self.change=False
    def read(self,at,n):
        self.calls+=1
        b=bytes(self.s[at:at+n])
        if self.change and self.calls==4:b=bytes([b[0]^1])+b[1:]
        return b


class Checks(unittest.TestCase):
    def test_layout_and_checksums_are_separate(self):
        p,bounds=fixture();self.assertEqual(bounds,(0x5100,0x85000))
        layout.verify_crcs_after_capture(p)
        self.assertEqual(p[0x50c0:0x50e0],layout.manifest())
        self.assertEqual(struct.unpack_from('<I',p,0x85008)[0]+0x85000,layout.OLD_END)
        for edge in (layout.START,layout.START+layout.LENGTH-1):
            q=bytearray(p);q[edge]^=0x80;layout.verify_crcs_after_capture(q)
        q=bytearray(p);q[0x8000]^=1
        with self.assertRaises(ValueError):layout.verify_crcs_after_capture(q)
        payload=bytearray(b'\xff'*0x165d40);payload[layout.APP_BYTES]=0
        with self.assertRaises(ValueError):layout.repack(p,p,0,payload,lambda *args:None)

    def test_export_decode_and_readonly_output(self):
        s=store();r=FakeReader(s)
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
            entries=host.export(r,tmp,{'status':0},lambda x:None)
            self.assertEqual(len(entries),1);p=Path(tmp)/'SCRN0001.BMP'
            im=Image.open(p);self.assertEqual(im.size,(240,240))
            self.assertEqual(im.convert('RGB').tobytes(),bytes([0,255,0])*57600)
            self.assertTrue(entries[0]['identical_second_acquisition'])
            with self.assertRaises(FileExistsError):host.export(r,tmp,{'status':0},lambda x:None)
            self.assertEqual(s,store())

    def test_corruption_and_power_cut_are_not_published(self):
        s=store();h=bytes(s[:64]);rec=host.record(h)
        for at in range(64,64+rec[1]):
            bad=bytearray(s[64:64+rec[1]]);bad[at-64]^=1
            with self.assertRaises(ValueError):host.bmp(bad,rec[2])
        for n in range(4):
            bad=bytearray(h);bad[60+n:]=b'\xff'*(4-n)
            self.assertIsNone(host.record(bytes(bad)))
        r=FakeReader(s);r.change=True
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
            with self.assertRaises(ValueError):host.export(r,tmp,{'status':0},lambda x:None)
            self.assertEqual(list(Path(tmp).iterdir()),[])
        for data in (b'\0\0',b'\xff\xff\0\0',b'\x01\0'):
            with self.assertRaises(ValueError):host.bmp(data,zlib.crc32(data))

    def test_c_wire_crc_sequence_view(self):
        wire=(ROOT/'build/capture-device/protocol-fixture.bin').read_bytes()
        info=decode(wire[:64],0,0,b'T3CR',2)
        self.assertEqual(struct.unpack_from('<4I',info),(0x90000,0xc0000,0x20000,6))
        self.assertEqual(decode(wire[64:],7,9,b'T3CR',2),bytes(range(7,47)))
        for i in range(64):
            bad=bytearray(wire[64:]);bad[i]^=1
            with self.assertRaises(ValueError):decode(bad,7,9,b'T3CR',2)
        with self.assertRaises(ValueError):decode(wire[64:],7,10,b'T3CR',2)
        with self.assertRaises(ValueError):decode(wire[64:],7,9)
        class Hid:
            def send_feature_report(self,q):
                assert len(q)==65 and q[:5]==b'\0T3CQ';self.q=q[1:];return 65
            def get_feature_report(self,report_id,n):
                assert report_id==0 and n==65
                q=self.q;a=bytearray(wire[:64] if q[4]==0 else wire[64:])
                a[8:12]=q[8:12];a[16:20]=q[12:16]
                a[12:16]=bytes(4);struct.pack_into('<I',a,12,zlib.crc32(a))
                return b'\0'+a
        reader=host.CaptureReader(Hid())
        self.assertEqual(reader.info()['slots'],6)
        self.assertEqual(reader.read(7,40),bytes(range(7,47)))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True);p.add_argument('--device-view',type=Path)
    p.add_argument('--native-io',type=Path,help='Replay storage calls from the linked Pi32v2 writer through the C NOR backend')
    a=p.parse_args()
    out=ROOT/'build/capture-device';out.mkdir(parents=True,exist_ok=True)
    plain,_=fixture();f=out/'layout-fixture.bin';f.write_bytes(plain[0x5000:0x5100]+plain[0x85000:0x85020])
    for name in ('capture_store','capture_button','capture_usb','button','flash_usb','board_capture_device'):
        binary=out/(name+'-test.exe');sources=[str(ROOT/f'tests/{name}.c')]
        flags=[]
        if name=='flash_usb':
            flags=['-DT3P_CAPTURE_DEVICE','-DT3P_BOOT_HOST'];sources.append(str(ROOT/'native/t3p_capture_usb.c'))
        subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror',*flags,*sources,'-o',str(binary)],check=True)
        args=[str(f)]+([str(a.native_io)] if a.native_io else []) if name=='capture_store' else [str(out/'protocol-fixture.bin')] if name=='capture_usb' else []
        subprocess.run([str(binary),*args],check=True)
    if a.device_view:layout.verify_device(a.device_view.read_bytes())
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checks))
    if not result.wasSuccessful():raise SystemExit(1)
    print('Offline capture device checks passed; hardware erase/program/power-cycle tests pending.')

if __name__=='__main__':main()
