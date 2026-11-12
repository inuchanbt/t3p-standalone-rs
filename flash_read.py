"""Read T3P Flash Reader V2's CPU mapped view through HID feature reports.

This is NOT a raw or complete physical flash backup. No erase/write commands.
Install Python package hidapi for hardware access; --help has no dependencies.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import zlib

PRODUCT='T3P Flash Reader V2'

def request(op, offset=0, length=0, sequence=0):
    q=bytearray(64);q[:4]=b'T3PQ';q[4]=op;q[6]=length
    struct.pack_into('<II',q,8,offset,sequence)
    return bytes(q)

def decode(a, offset, sequence):
    if len(a)!=64 or a[:5]!=b'T3PR\x01' or a[7]!=1:
        raise ValueError('Unexpected reader response/view')
    checksum=struct.unpack_from('<I',a,12)[0]
    clean=bytearray(a);clean[12:16]=bytes(4)
    if zlib.crc32(clean)!=checksum:raise ValueError('Response CRC mismatch')
    if struct.unpack_from('<I',a,8)[0]!=offset or struct.unpack_from('<I',a,16)[0]!=sequence:
        raise ValueError('Stale response address/sequence')
    if a[5]:raise ValueError(f'Device reader error {a[5]}')
    if a[6]>40 or any(a[20:24]) or any(a[24+a[6]:]):raise ValueError('Malformed response length/padding')
    return a[24:24+a[6]]

class Reader:
    def __init__(self, device):self.device=device;self.sequence=0
    def exchange(self, op, offset=0, length=0):
        self.sequence=(self.sequence+1)&0xffffffff
        q=request(op,offset,length,self.sequence)
        last=None
        for attempt in range(3):
            try:
                if self.device.send_feature_report(b'\0'+q)!=65:raise OSError('Short HID request')
                raw=bytes(self.device.get_feature_report(0,65))
                if len(raw)!=65 or raw[0]!=0:raise ValueError('Short HID response/report ID')
                data=decode(raw[1:],offset,self.sequence)
                if len(data)!=(40 if op==0 else length):raise ValueError('Short reader data')
                return data
            except (OSError,ValueError) as error:
                last=error
                if attempt<2:time.sleep(0.05)
        raise OSError(f'Reader request failed at view offset {offset:#x}: {last}')
    def info(self):
        fields=struct.unpack('<10I',self.exchange(0))
        keys=('cpu_base','physical_base','view_bytes','sfc_con0','sfc_offset','ndec_start','ndec_end','ready','reserved0','reserved1')
        info=dict(zip(keys,fields))
        if fields[8:]!=(0,0):raise ValueError('Unknown metadata version')
        if info['ready']!=1:raise ValueError('Device mapping is LOCKED')
        if fields[:3]!=(0xc000000,0x5000,0x1fa000) or info['sfc_offset']!=0xf4005000:
            raise ValueError('Unexpected mapping; no output created')
        return info

def save(reader, output, start, count, info, progress=None):
    if start<0 or count<=0 or start>info['view_bytes'] or count>info['view_bytes']-start:
        raise ValueError('Requested range is outside the mapped view')
    output=Path(output);partial=Path(str(output)+'.partial');manifest=Path(str(output)+'.json')
    if output.exists() or manifest.exists():raise FileExistsError('Output already exists')
    digest=hashlib.sha256()
    with partial.open('xb') as f:
        for offset in range(start,start+count,40):
            data=reader.exchange(1,offset,min(40,start+count-offset));f.write(data);digest.update(data)
            if progress and (offset-start)%65520==0:progress('read',offset-start,count)
        f.flush();os.fsync(f.fileno())
    # Independent second acquisition must match every byte before publication.
    with partial.open('rb') as f:
        for offset in range(start,start+count,40):
            data=reader.exchange(1,offset,min(40,start+count-offset))
            if f.read(len(data))!=data:raise ValueError(f'Second acquisition differs at {offset:#x}')
            if progress and (offset-start)%65520==0:progress('verify',offset-start,count)
    result=dict(format='T3P CPU MAPPED VIEW V1; NOT A RAW OR COMPLETE FLASH BACKUP',
                device_info=info,view_offset=start,bytes=count,
                physical_start=info['physical_base']+start,
                cpu_start=info['cpu_base']+start,sha256=digest.hexdigest(),
                identical_second_acquisition=True,flash_write_commands=False)
    # Exclusive outputs: never silently replace an earlier acquisition.
    with manifest.open('x',encoding='utf-8') as f:f.write(json.dumps(result,indent=2)+'\n')
    partial.rename(output)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--info',action='store_true',help='Read metadata only')
    p.add_argument('--output',type=Path,help='Destination, suggested suffix .mapped.bin')
    p.add_argument('--start',type=lambda s:int(s,0),default=0,help='Offset relative to CPU mapped view')
    p.add_argument('--length',type=lambda s:int(s,0),help='Bytes; default is the rest of the mapped view')
    a=p.parse_args()
    if not a.info and not a.output:p.error('Use --info or --output')
    import hid
    matches=[d for d in hid.enumerate(0x483,0x5750) if d.get('product_string')==PRODUCT]
    if len(matches)!=1:raise SystemExit(f'Expected one {PRODUCT}; found {len(matches)}. Use only the reader trial firmware.')
    dev=hid.device()
    try:
        dev.open_path(matches[0]['path']);r=Reader(dev);info=r.info()
        print(json.dumps(info,indent=2),flush=True)
        if a.output:
            print('Acquiring mapped view twice; wait for SHA-256 result.',flush=True)
            def progress(phase,done,total):print(f'{phase}: {done:,}/{total:,} bytes',flush=True)
            print(json.dumps(save(r,a.output,a.start,a.length if a.length is not None else info['view_bytes']-a.start,info,progress),indent=2))
    finally:dev.close()

if __name__=='__main__':main()
