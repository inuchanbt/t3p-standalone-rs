"""Compare linked V4-path/V5 writer traces through the physical NOR model.

These are constructed frames and model operation counts, never device timings.
Run both pack_demo validations (or check_pi32 capture checks) first.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def trace(path):
    data = path.read_bytes()
    at = 0
    calls = [0] * 4
    requested = [0] * 4
    while at < len(data):
        op, offset, n = struct.unpack_from('<BII', data, at)
        at += 9
        assert op < 4
        assert n <= 258
        if op >= 2:
            assert n == 0 and (offset == 0 if op == 2 else offset < 6)
        else:
            assert offset + n <= 0xc0000 and at + n <= len(data)
            at += n
        calls[op] += 1
        requested[op] += n
    assert at == len(data)
    return dict(calls=dict(zip(('read', 'program', 'flush', 'blank_claim'), calls)),
                requested_bytes=dict(zip(('read', 'program', 'flush', 'blank_claim'), requested)))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', required=True)
    p.add_argument('--baseline', type=Path, default=ROOT/'build/capture-v5-baseline')
    p.add_argument('--optimized', type=Path, default=ROOT/'build/capture-v5')
    a = p.parse_args()
    result = dict(format='OFFLINE SYNTHETIC COUNTS; NOT DEVICE TIMINGS')
    images = []
    for name, directory in (('baseline', a.baseline), ('optimized', a.optimized)):
        flow = directory/'OFFLINE-native-capture-io.bin'
        values = trace(flow)
        binary = directory/'comparison-nor-test.exe'
        flags = ['-DT3P_CAPTURE_TIMING_BASELINE'] if name == 'baseline' else []
        subprocess.run([a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', *flags,
                        str(ROOT/'tests/capture_store.c'), '-o', str(binary)], check=True)
        replay = subprocess.run([str(binary), str(a.optimized/'layout-fixture.bin'), str(flow)],
                                check=True, capture_output=True, text=True)
        print(name + ': ' + replay.stdout.strip(), flush=True)
        m = re.search(r'(\d+) calls, (\d+) page programs, max (\d+) per page', replay.stdout)
        assert m and int(m[1]) == sum(values['calls'].values())
        values.update(page_programs=int(m[2]), maximum_programs_per_page=int(m[3]))
        image = (directory/'OFFLINE-native-capture.BMP').read_bytes()
        assert len(image) == 115266
        images.append(image)
        values['bmp_sha256'] = hashlib.sha256(image).hexdigest()
        result[name] = values
    before, after = result['baseline'], result['optimized']
    assert images[0] == images[1], 'Optimization changed the saved pixels'
    assert before['calls']['blank_claim'] == 0 and after['calls']['blank_claim'] == 1
    assert before['calls']['flush'] == 30 and after['calls']['flush'] == 0
    assert before['requested_bytes']['read'] - after['requested_bytes']['read'] == 0x20000
    assert before['requested_bytes']['program'] == after['requested_bytes']['program']
    assert after['page_programs'] < before['page_programs']
    assert after['maximum_programs_per_page'] <= before['maximum_programs_per_page']
    (a.optimized/'OFFLINE-comparison.json').write_text(json.dumps(result, indent=2)+'\n')
    print('Identical BMP pixels; one redundant slot read removed; fewer physical page programs.')


if __name__ == '__main__':
    main()
