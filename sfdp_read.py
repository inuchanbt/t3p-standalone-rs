"""Acquire the read-only T3P SFDP Probe V1 boot snapshot and compare its tables.

No write command exists. The device compares two independent SPI acquisitions;
this tool also compares two USB acquisitions. A bounded snapshot is complete
only when every advertised parameter header and table fits inside it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from capture_export import CaptureReader

PRODUCT = 'T3P SFDP Probe V1'
SIZE = 4096
REFERENCE_SOURCE = ('https://www.puyasemi.com/download_path/'
                    '%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/Flash/P25Q16H_Datasheet_V2.1.pdf')
# Manufacturer transcript, NOT device evidence. Same nine DWORDs as the C guard.
REFERENCE_HEADER = bytes.fromhex('53464450000101ff00000109300000ff85000103600000ff')
REFERENCE_BFPT = bytes.fromhex(
    'e520f1ff ffffff00 44eb086b 083b80bb eeffffff ffff00ff ffff00ff 0c200f52 10d80881')


class SfdpReader(CaptureReader):
    metadata_ops = (0, 2, 4)

    def diagnostics(self):
        result = super().diagnostics()
        names = {10: 'SFDP snapshot first SPI pass', 11: 'SFDP snapshot second SPI pass',
                 12: 'read-only probe; matching profile still locked'}
        if result['step'] in names:
            result['step_name'] = names[result['step']]
        if result['step'] in (10, 11):
            result['density_word'] = result['page_word'] = None
        if result['io_error'] == 4:
            result['io_error_name'] = 'Independent SPI acquisition differs'
        return result

    def snapshot_info(self):
        f = struct.unpack('<10I', self.exchange(4))
        if f[0] != 0x31444653 or f[4] != 1 or any(f[6:]):
            raise ValueError('Unexpected SFDP probe metadata/version')
        if f[1] not in (0, SIZE) or f[3] != (2 if f[1] else 0) or (not f[1] and f[2]):
            raise ValueError('Incomplete SFDP acquisition metadata')
        return dict(bytes=f[1], crc32=f[2], matching_spi_acquisitions=f[3],
                    flash_mutation_code_excluded=True, jedec=f[5])

    def snapshot(self):
        return b''.join(self.exchange(3, at, min(40, SIZE-at)) for at in range(0, SIZE, 40))


def tables(data):
    if len(data) != SIZE or data[:4] != b'SFDP' or data[5] != 1:
        raise ValueError('Invalid SFDP snapshot signature/major version/length')
    count = data[6]+1
    end = 8+count*8
    if end > len(data):
        raise ValueError('Parameter headers extend beyond acquired snapshot')
    result = []
    for i in range(count):
        h = data[8+i*8:16+i*8]
        at = int.from_bytes(h[4:7], 'little')
        size = h[3]*4
        if not size or at < end or at+size > len(data):
            raise ValueError(f'Parameter {i} is outside acquired table extent: {at:#x}+{size}')
        raw = data[at:at+size]
        item = dict(index=i, parameter_id=f'{h[7]:02X}{h[0]:02X}',
                    major=h[2], minor=h[1], dwords=h[3], pointer=at,
                    header=h.hex(), data=raw.hex(), sha256=hashlib.sha256(raw).hexdigest())
        if item['parameter_id'] == 'FF00':
            item['manufacturer_reference'] = dict(source=REFERENCE_SOURCE, pages='54-56',
                kind='published reference, not device acquisition', data=REFERENCE_BFPT.hex(),
                exact_match=raw == REFERENCE_BFPT,
                differences=[dict(byte_offset=n, sfdp_address=at+n,
                    expected=REFERENCE_BFPT[n] if n<len(REFERENCE_BFPT) else None,
                    actual=raw[n] if n<len(raw) else None)
                    for n in range(max(len(raw),len(REFERENCE_BFPT)))
                    if raw[n:n+1] != REFERENCE_BFPT[n:n+1]])
            if len(raw) >= 36:
                first, density = struct.unpack_from('<II', raw)
                item['density_word'] = f'{density:08X}'
                item['address_bytes_bits'] = (first >> 17) & 3
                item['erase_types'] = [dict(exponent=raw[n], opcode=f'{raw[n+1]:02X}')
                                       for n in range(28,36,2)]
                item['page_word'] = f'{struct.unpack_from("<I",raw,40)[0]:08X}' if len(raw)>=44 else None
        result.append(item)
    if not any(t['parameter_id'] == 'FF00' for t in result):
        raise ValueError('No basic flash parameter table advertised')
    header = data[:24]
    return dict(sfdp_header=data[:8].hex(), major=data[5], minor=data[4],
                manufacturer_reference_header=dict(source=REFERENCE_SOURCE,
                    kind='published reference, not device acquisition',
                    expected=REFERENCE_HEADER.hex(), actual=header.hex(),
                    exact_match=header == REFERENCE_HEADER,
                    differences=[dict(sfdp_address=n,expected=b,actual=header[n])
                                 for n,b in enumerate(REFERENCE_HEADER) if header[n]!=b]),
                parameter_headers=count, all_advertised_tables_acquired=True, tables=result)


def acquire(reader):
    before = reader.snapshot_info()
    if before['bytes'] != SIZE:
        raise ValueError('Device has no stable two-pass SPI snapshot; inspect diagnostics')
    info = reader.info()
    if info['jedec'] != before['jedec'] or info['status'] == 0:
        raise ValueError('Probe identity/locked state disagrees')
    diagnostics = reader.diagnostics()
    data = reader.snapshot()
    if reader.snapshot() != data or reader.snapshot_info() != before:
        raise ValueError('Second USB acquisition or metadata changed')
    if zlib.crc32(data) != before['crc32']:
        raise ValueError('SFDP snapshot CRC32 differs from boot acquisition')
    report = dict(format='T3P read-only SFDP acquisition v1', product=PRODUCT,
                  sha256=hashlib.sha256(data).hexdigest(), identical_second_usb_acquisition=True,
                  snapshot=before, device_info=info, diagnostics=diagnostics, **tables(data))
    return data, report


def save(output, data, report):
    output = Path(output)
    sidecar = Path(str(output)+'.json')
    if output.exists() or sidecar.exists():
        raise FileExistsError(f'Output exists: {output}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as f:
        f.write(data)
    with sidecar.open('x', encoding='utf-8') as f:
        f.write(json.dumps(report, indent=2)+'\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--info', action='store_true')
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    if not a.info and a.output is None:
        p.error('Use --info or --output FILE.bin')
    import hid
    devices = [d for d in hid.enumerate(0x483,0x5750)
               if d.get('product_string') == PRODUCT and d.get('usage_page') == 0xff31
               and d.get('usage') == 1 and d.get('interface_number') == 0]
    if len(devices) != 1:
        raise SystemExit(f'Expected one {PRODUCT}; found {len(devices)}')
    dev = hid.device()
    try:
        dev.open_path(devices[0]['path'])
        r = SfdpReader(dev)
        if a.output:
            data, report = acquire(r)
            save(a.output, data, report)
            print(json.dumps(report, indent=2), flush=True)
        else:
            print(json.dumps(dict(device_info=r.info(), diagnostics=r.diagnostics(),
                                  snapshot=r.snapshot_info()), indent=2), flush=True)
    finally:
        dev.close()


if __name__ == '__main__':
    main()
