"""Reconcile device mapping with stock reserved entries; never authorize writes."""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def inspect(plain, context, offset):
    if (context[1] - context[2]) & 0xffffffff != offset:
        raise ValueError('Device mapping disagrees with SFC')
    entries = []
    for at in range(0x5020, 0x50c0, 32):
        _, _, start, size, flags, _, _, name = struct.unpack_from('<HHIIBBH16s', plain, at)
        entries.append(dict(name=name.split(b'\0')[0].decode('ascii'), offset=start,
                            size=size, flags=flags))
    vm = next(e for e in entries if e['name'] == 'VM')
    return dict(status='NO WRITABLE PARTITION VERIFIED', mapping_offset=offset,
                entry_physical=(0x0c000100 + offset) & 0xffffffff,
                entries=entries,
                vm_possible_starts=[vm['offset'], vm['offset'] + context[1]],
                key_mac_boundary=0x1ff000, flash_writes_enabled=False,
                unresolved=['VM address semantics and ownership',
                            'raw/encrypted flash access and cache coherency',
                            'device-specific data outside distributed image'])

def main():
    words = {}
    for line in (ROOT / 'tests/capture_preflight_device.words').read_text().splitlines():
        if line and not line.startswith('#'):
            fields = line.split()
            words[fields[0]] = [int(v, 16) for v in fields[1:]]
    source = ROOT.parent / 'doom_on_t3p/local/unpacked/T3P Firmware Version 3.0.3.bin/decrypted.bin'
    report = inspect(source.read_bytes(), words['CONTEXT'], words['SFC'][2])
    out = ROOT / 'build/capture-probe/partition-audit.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
