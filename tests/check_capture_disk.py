"""Independently walk generated FAT16 chains and decode BMPs using Pillow.

Input is an OFFLINE fixture, never a USB drive or a firmware image.
Run after the capture_disk example with NAME=PPM pairs in the same order.
"""
import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
import struct
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()
    raw = args.image.read_bytes()
    u16 = lambda offset: struct.unpack_from("<H", raw, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", raw, offset)[0]
    sector, cluster_sectors, reserved, fats = u16(11), raw[13], u16(14), raw[16]
    roots, total, fat_sectors = u16(17), u16(19) or u32(32), u16(22)
    assert sector == 512 and raw[510:512] == b"\x55\xaa"
    assert len(raw) == total * sector and fats == 2
    root_start = reserved + fats * fat_sectors
    data_start = root_start + (roots * 32 + sector - 1) // sector
    clusters = (total - data_start) // cluster_sectors
    assert 4085 <= clusters < 65525, "BPB must actually describe FAT16"
    fat = raw[reserved * sector:(reserved + fat_sectors) * sector]
    assert fat == raw[(reserved + fat_sectors) * sector:root_start * sector]
    entries = []
    for pos in range(root_start * sector, root_start * sector + roots * 32, 32):
        e = raw[pos:pos + 32]
        if e[0] == 0:
            break
        if e[11] == 8:
            continue
        assert e[11] == 0x21, "BMP must be read-only archive"
        entries.append(e)
    assert len(entries) == len(args.inputs)
    allocated = set()
    results = []
    for sequence, (e, reference) in enumerate(zip(entries, args.inputs), 1):
        prefix, path = reference.split("=", 1)
        assert e[:11] == f"{prefix}{sequence:04d}BMP".encode("ascii")
        name = e[:8].decode("ascii") + ".BMP"
        size = struct.unpack_from("<I", e, 28)[0]
        cluster = struct.unpack_from("<H", e, 26)[0]
        data = bytearray()
        while cluster < 0xfff8:
            assert 2 <= cluster < clusters + 2 and cluster not in allocated
            allocated.add(cluster)
            begin = (data_start + (cluster - 2) * cluster_sectors) * sector
            data += raw[begin:begin + cluster_sectors * sector]
            cluster = struct.unpack_from("<H", fat, cluster * 2)[0]
        assert len(data) - cluster_sectors * sector < size <= len(data)
        assert not any(data[size:]), "file tail must not leak bytes"
        bmp = bytes(data[:size])
        actual = Image.open(BytesIO(bmp)).convert("RGB")
        expected = Image.open(path).convert("RGB")
        assert actual.size == expected.size == (240, 240)
        # Compare the original RGB565 channel bits independently of the
        # decoder's RGB888 expansion/rounding convention.
        def rgb565(image):
            rgb = image.tobytes()
            return [(r >> 3, g >> 2, b >> 3) for r, g, b in zip(rgb[::3], rgb[1::3], rgb[2::3])]
        assert rgb565(actual) == rgb565(expected), name
        args.image.with_name(name).write_bytes(bmp)
        results.append(dict(name=name, width=240, height=240, rgb565_pixels_equal=True,
                            sha256=hashlib.sha256(bmp).hexdigest()))
    for cluster in range(2, clusters + 2):
        value = struct.unpack_from("<H", fat, cluster * 2)[0]
        assert (value != 0) == (cluster in allocated), "orphan allocation"
    report = dict(status="OFFLINE ONLY; no device capture, USB enumeration or flash writes",
                  fat16_clusters=clusters, images=results)
    args.image.with_name("verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
