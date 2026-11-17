"""Offline ENS220 state, actual I2C framing and V3 diagnostic semantics checks."""
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


def decode(words):
    data=struct.pack('<110I',*words)
    trailer=struct.pack('<10I',host.MAGIC,words[1],110,words[2],zlib.crc32(data),0,0,0,0,0)
    return host.decode(data,trailer)


class Checks(unittest.TestCase):
    def words(self):return [int(x,16) for x in (ROOT/'tests/ens220_diag.words').read_text().split()]
    def test_kelvin_counts_have_no_BMP_calibration_or_ADC_claim(self):
        d=decode(self.words())
        self.assertEqual(d['sensor'],'ENS220');self.assertEqual(d['ID'],'0321')
        self.assertEqual(d['temperature_C'],25);self.assertEqual(d['raw_temperature_counts'],0x9513)
        self.assertIsNone(d['calibration']);self.assertIsNone(d['raw_ADC'])
        self.assertEqual(d['bus'],'PB2 SCL / PB1 SDA; wire 40/41; 7-bit 20')
        self.assertEqual(d['configuration_readback_mask'],'0F')
        self.assertEqual(d['cleanup']['state'],'idle verified')
    def test_unknown_sensor_or_BMP_flags_rejected(self):
        for word,value in ((39,0),(39,1),(18,31),(21,0x10000),(22,16),(23,5)):
            w=self.words();w[word]=value
            with self.assertRaises(ValueError):decode(w)
    def test_unread_and_valid_zero_are_distinct(self):
        w=self.words();w[18]=0;w[36]=0
        d=decode(w);self.assertIsNone(d['ID']);self.assertIsNone(d['temperature_C'])
        self.assertIsNone(d['raw_temperature_counts'])
        w=self.words();w[35]=0;w[21]=34963
        self.assertEqual(decode(w)['temperature_C'],0)
    def test_primary_failure_and_secondary_cleanup_remain_separate(self):
        w=self.words();w[8]=1;w[12]=4;w[13]=4;w[36]=0
        w[23]=3|(4<<8)|(3<<16)
        d=decode(w)
        self.assertEqual(d['stage'],'register ACK');self.assertEqual(d['bus_error'],'NACK')
        self.assertEqual(d['cleanup']['state'],'transport failure')
        self.assertEqual(d['cleanup']['stage'],'write address ACK')
    def test_actual_device_V2_snapshot_remains_decodable(self):
        import json
        p=ROOT/'build/temperature-probe-v2/device-probe-01.json'
        if not p.exists():self.skipTest('Local V2 physical snapshot not present')
        j=json.loads(p.read_text())
        words=[int(x,16) for x in j['temperature']['raw_words']]
        self.assertEqual(decode(words)['snapshot_crc32'],j['temperature']['snapshot_crc32'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True)
    a=p.parse_args();out=ROOT/'build/temperature-ens220-v3';out.mkdir(exist_ok=True)
    common=['-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-DT3P_SYSTEM','-DT3P_TEMPERATURE_DIAGNOSTICS','-DT3P_TEMPERATURE_ENS220']
    cases=[('ens220-state',['tests/ens220.c','native/t3p_temperature.c','native/t3p_ens220.c'],[]),
        ('ens220-transport',['tests/ens220_transport.c','native/t3p_meter.c','native/t3p_temperature.c','native/t3p_ens220.c'],
            ['-DT3P_MEASURE','-DT3P_PD_PROFILES','-DT3P_PD_CABLE_READ','-DT3P_PD_CHARGER'])]
    for name,sources,flags in cases:
        binary=out/(name+'.exe')
        subprocess.run([a.cc,*common,*flags,*sources,'-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True);print(name+': PASS',flush=True)
    if not unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checks)).wasSuccessful():
        raise SystemExit(1)


if __name__=='__main__':main()
