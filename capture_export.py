"""Export committed T3P Capture screenshots as BMP via read-only USB HID.

No flash erase/program command exists in this protocol. Requires hidapi only
for hardware access. Existing output files are never replaced.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from flash_read import Reader

PRODUCTS=('T3P Capture V1','T3P Capture V2')
SLOT=0x20000


class CaptureReader(Reader):
    query_magic=b'T3CQ'
    response_magic=b'T3CR'
    view=2
    metadata_ops=(0,2)

    def info(self):
        f=struct.unpack('<10I',self.exchange(0))
        if f[:4]!=(0x90000,0xc0000,SLOT,6) or f[8:]!=(0,0):
            raise ValueError('Unexpected capture allocation/version')
        info=dict(zip(('physical_start','bytes','slot_bytes','slots','status','jedec','sfdp','last_result','reserved0','reserved1'),f))
        if info['last_result']&0x80000000:info['last_result']-=0x100000000
        return info

    def read(self, at, count):
        if at<0 or count<0 or at+count>6*SLOT:raise ValueError('Capture bounds')
        return b''.join(self.exchange(1,p,min(40,at+count-p)) for p in range(at,at+count,40))

    def diagnostics(self):
        f=struct.unpack('<10I',self.exchange(2))
        steps=('ready','layout','JEDEC read/ID','SFDP transfer','SFDP signature',
               'SFDP header','BFPT transfer','density/addressing','4K erase','page/profile')
        errors=('none','cache idle timeout','SFC idle timeout','SPI byte timeout')
        return dict(step=f[0],step_name=steps[f[0]] if f[0]<len(steps) else 'unknown',
                    io_error=f[1],io_error_name=errors[f[1]] if f[1]<len(errors) else 'unknown',
                    bfpt_dwords=f[2],bfpt_pointer=f[3],
                    sfdp_header=struct.pack('<4I',*f[4:8]).hex() if f[0]==0 or f[0]>=4 else None,
                    density_word=f'{f[8]:08X}' if f[0]==0 or f[0]>=7 else None,
                    page_word=f'{f[9]:08X}' if f[2]>=11 and (f[0]==0 or f[0]>=7) else None)


def record(h):
    if len(h)!=64:raise ValueError('Short capture header')
    if h[:8]!=b'T3PSCR01' or h[60:]!=b'DONE':return None
    if zlib.crc32(h[:40])!=struct.unpack_from('<I',h,40)[0]:raise ValueError('Capture header CRC')
    seq=struct.unpack_from('<H',h,8)[0];size,checksum=struct.unpack_from('<II',h,32)
    if (not 1<=seq<=9999 or not all(65<=b<=90 for b in h[10:14])
            or any(h[14:32]) or h[44:60]!=b'\xff'*16 or not 0<size<=SLOT-64):
        raise ValueError('Malformed capture header')
    return h[10:14].decode()+f'{seq:04d}.BMP',size,checksum,seq


def bmp(data, checksum):
    if zlib.crc32(data)!=checksum:raise ValueError('Capture payload CRC')
    pixels=bytearray();at=0
    while at<len(data):
        if at+2>len(data):raise ValueError('Short codec token')
        token=struct.unpack_from('<H',data,at)[0];at+=2;count=token&0x7fff
        if count==0 or len(pixels)+count*2>115200:raise ValueError('Invalid codec count')
        n=2 if token&0x8000 else count*2
        if at+n>len(data):raise ValueError('Short codec payload')
        pixels+=data[at:at+n]*(count if token&0x8000 else 1);at+=n
    if len(pixels)!=115200:raise ValueError('Incomplete capture')
    h=bytearray(66);h[:2]=b'BM'
    struct.pack_into('<I',h,2,115266);struct.pack_into('<I',h,10,66)
    struct.pack_into('<IiiHHII',h,14,40,240,-240,1,16,3,115200)
    struct.pack_into('<III',h,54,0xf800,0x7e0,0x1f)
    return bytes(h+pixels)


def export(reader, output, info, progress=print):
    if info['status']!=0:raise ValueError(f"Capture backend is LOCKED (status {info['status']}, JEDEC {info['jedec']:06X}, SFDP {info['sfdp']:08X})")
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    entries=[];seen=set()
    for slot in range(6):
        at=slot*SLOT;h=reader.read(at,64);r=record(h)
        if r is None:continue
        name,size,checksum,seq=r
        if seq in seen:raise ValueError('Duplicate capture sequence')
        seen.add(seq)
        dest=output/name;sidecar=Path(str(dest)+'.json')
        if dest.exists() or sidecar.exists():raise FileExistsError(f'Output exists: {dest}')
        progress(f'Reading {name} ({size:,} stored bytes)')
        data=reader.read(at+64,size)
        if reader.read(at,64)!=h or reader.read(at+64,size)!=data:
            raise ValueError('Independent second acquisition differs')
        image=bmp(data,checksum)
        result=dict(file=name,bytes=len(image),sha256=hashlib.sha256(image).hexdigest(),
                    stored_bytes=size,slot=slot,identical_second_acquisition=True,device_info=info)
        partial=Path(str(dest)+'.partial')
        with partial.open('xb') as f:f.write(image)
        with sidecar.open('x',encoding='utf-8') as f:f.write(json.dumps(result,indent=2)+'\n')
        partial.rename(dest);entries.append(result)
    return entries


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--info',action='store_true');p.add_argument('--output',type=Path)
    a=p.parse_args()
    if not a.info and a.output is None:p.error('Use --info or --output DIRECTORY')
    import hid
    devices=[d for d in hid.enumerate(0x483,0x5750) if d.get('product_string') in PRODUCTS]
    if len(devices)!=1:raise SystemExit(f'Expected one T3P Capture V1/V2; found {len(devices)}')
    dev=hid.device()
    try:
        dev.open_path(devices[0]['path']);r=CaptureReader(dev);info=r.info()
        if devices[0]['product_string']==PRODUCTS[1]:info['diagnostics']=r.diagnostics()
        print(json.dumps(info,indent=2),flush=True)
        if a.output:
            entries=export(r,a.output,info)
            print(f'Exported {len(entries)} screenshots to {a.output.resolve()}',flush=True)
    finally:dev.close()

if __name__=='__main__':main()
