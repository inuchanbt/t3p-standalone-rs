"""Host decoder and real C USB temperature protocol integration, offline only."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import zlib
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
import temperature_read as host
from flash_read import decode as envelope


def fixture():
    words=[int(w,16) for w in (ROOT/'tests/temperature_diag.words').read_text().split()]
    data=struct.pack('<110I',*words)
    trailer=struct.pack('<10I',host.MAGIC,1,110,words[2],zlib.crc32(data),0,0,0,0,0)
    return words,data,trailer


class Checks(unittest.TestCase):
    wire=None
    def test_validity_flags_distinguish_unread_and_zero(self):
        w,data,trailer=fixture();d=host.decode(data,trailer)
        self.assertIsNone(d['ID']);self.assertIsNone(d['raw_ADC']);self.assertIsNone(d['temperature_C'])
        self.assertEqual(d['stage'],'write address ACK');self.assertFalse(d['ACKs']['write_address'])
        self.assertIsNone(d['ACKs']['register'])
        w[18]=31;w[19]=0x58;w[36]=1 # Real zero degrees is valid.
        data=struct.pack('<110I',*w);t=list(struct.unpack('<10I',trailer));t[4]=zlib.crc32(data)
        d=host.decode(data,struct.pack('<10I',*t))
        self.assertEqual(d['ID'],'58');self.assertEqual(d['temperature_C'],0)
        self.assertEqual(d['raw_ADC'],0);self.assertEqual(d['calibration'],'000000000000')

    def test_corrupt_or_mixed_snapshot_rejected(self):
        _,data,trailer=fixture()
        for index in (0,8,100,439):
            bad=bytearray(data);bad[index]^=1
            with self.assertRaises(ValueError):host.decode(bytes(bad),trailer)
        bad=bytearray(trailer);bad[12]^=1
        with self.assertRaises(ValueError):host.decode(data,bytes(bad))

    def test_c_USB_cache_round_trip_without_sensor_or_storage(self):
        reports=self.wire.read_bytes();self.assertEqual(len(reports),18*64)
        frames=[reports[i:i+64] for i in range(0,len(reports),64)]
        pages=[envelope(f,p,9,b'T3CR',2) for p,f in enumerate(frames[6:])]
        d=host.decode(b''.join(pages[:11]),pages[11])
        self.assertEqual(d['attempts'],0) # Live cache mutated after page zero.
        self.assertIsNone(d['ID']);self.assertIsNone(d['GPIO']['PB_released'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True)
    a=p.parse_args();out=ROOT/'build/temperature-diagnostics-v1';out.mkdir(exist_ok=True)
    binary=out/'usb-temperature-check.exe';Checks.wire=out/'usb-temperature-check.frames'
    subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
        '-Wl,--gc-sections','-DT3P_SYSTEM','-DT3P_TEMPERATURE_DIAGNOSTICS',
        'tests/capture_usb.c','native/t3p_temperature.c','native/t3p_meter.c','-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary),str(Checks.wire)],cwd=ROOT,check=True)
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(Checks)
    if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():raise SystemExit(1)


if __name__=='__main__':main()
