"""Check bounded probe scheduling, GPIO restoration and C USB/host round-trip."""
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


class Checks(unittest.TestCase):
    wire=None
    def payload(self):
        reports=self.wire.read_bytes();self.assertEqual(len(reports),98*64)
        pages=[envelope(reports[i*64:(i+1)*64],i,123,b'T3CR',2) for i in range(98)]
        return b''.join(pages[:97]),pages[97]
    def test_c_wire_and_read_validity(self):
        data,trailer=self.payload();r=host.decode_probes(data,trailer)
        self.assertEqual(r['completed'],8);self.assertFalse(r['halted'])
        self.assertEqual([x['value'] for x in r['records'][:3]],['2260','91','58'])
        self.assertTrue(all(r['records'][i]['value'] is None for i in (3,4,6,7)))
        self.assertTrue(all(r['records'][i]['PA_restored'] for i in (4,6,7)))
        self.assertIsNone(r['records'][5]['PA_restored'])
        self.assertEqual(r['records'][5]['value'],'2103')
        self.assertEqual(r['records'][5]['part_ID_little_endian'],'0321')
        self.assertEqual(r['records'][6]['trace']['bus'],'PA6 SCL / PA5 SDA; wire EC/ED; 7-bit 76')
        self.assertIn('PA_before_init',r['records'][6]['trace']['GPIO'])
        self.assertIn('PB_before_init',r['records'][0]['trace']['GPIO'])
    def test_corrupt_and_mixed_snapshots_rejected(self):
        data,trailer=self.payload()
        for i in (0,30,500,len(data)-1):
            bad=bytearray(data);bad[i]^=1
            with self.assertRaises(ValueError):host.decode_probes(bad,trailer)
        bad=bytearray(trailer);bad[12]^=1
        with self.assertRaises(ValueError):host.decode_probes(data,bad)
    def test_unknown_probe_target_rejected_even_with_valid_crc(self):
        data,trailer=self.payload();bad=bytearray(data)
        struct.pack_into('<I',bad,(10+1)*4,0x41)
        t=list(struct.unpack('<10I',trailer));t[4]=zlib.crc32(bad)
        with self.assertRaises(ValueError):host.decode_probes(bad,struct.pack('<10I',*t))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True)
    a=p.parse_args();out=ROOT/'build/temperature-probe-v2';out.mkdir(exist_ok=True)
    common=['-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
        '-Wl,--gc-sections','-DT3P_SYSTEM','-DT3P_TEMPERATURE_DIAGNOSTICS','-DT3P_TEMPERATURE_PROBE']
    Checks.wire=out/'probe-usb-check.frames'
    cases=[('probe-engine',['tests/temperature_probe.c','native/t3p_temperature_probe.c','native/t3p_temperature.c'],[str(Checks.wire)]),
        ('probe-gpio',['tests/temperature_probe_gpio.c','native/t3p_meter.c','native/t3p_temperature.c'],[])]
    for name,sources,args in cases:
        binary=out/(name+'.exe')
        subprocess.run([a.cc,*common,*sources,'-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary),*args],cwd=ROOT,check=True);print(name+': PASS',flush=True)
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(Checks)
    if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():raise SystemExit(1)


if __name__=='__main__':main()
