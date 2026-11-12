"""Allocate capture storage from the old app file, never from stock VM/settings.

The updater still has the original length. app.bin and app_area_head CRCs end
before the new reserved extent. Existing resource/VM/BTIF/EXIF offsets stay put.
"""
from binascii import crc_hqx
import hashlib
import struct
import zlib

BASE = 0x5000
AREA_BYTES = 0x80000
APP_BYTES = AREA_BYTES - 0x100
RESERVED = BASE + AREA_BYTES
START = 0x90000
LENGTH = 0xc0000
SLOT_BYTES = 0x20000
OLD_END = 0x16ae40
DEVICE_SHA = 'ecbbe8bfbb9dfc07f40650d6435a8015e63c4e619394218a434f8d77f22be274'


def manifest():
    h = bytearray(struct.pack('<8s5I', b'T3PCAP01', START, LENGTH, SLOT_BYTES, 1, AREA_BYTES))
    return bytes(h + struct.pack('<I', zlib.crc32(h)))


def header(name, offset, size, flags, data_crc=0, index=0, reserved=0):
    h = bytearray(struct.pack('<HHIIBBH16s', 0, data_crc, offset, size, flags, reserved, index, name))
    struct.pack_into('<H', h, 0, crc_hqx(h[2:], 0))
    return h


def verify_device(view):
    if len(view) != 0x1fa000 or hashlib.sha256(view).hexdigest() != DEVICE_SHA:
        raise ValueError('Expected the twice-verified V3 device acquisition')
    for at, name, offset, size in (
        (0x20, b'app.bin', 0x100, 0x165d40),
        (0x40, b'VM', 0x178000, 0x7a000),
        (0x60, b'PRCT', 0, 0x178000),
        (0x80, b'BTIF', 0x1f2000, 0x1000),
        (0xa0, b'EXIF', 0x1f3000, 0x1000),
    ):
        h = view[at:at+32]
        if (crc_hqx(h[2:], 0) != struct.unpack_from('<H', h)[0]
                or struct.unpack_from('<II', h, 4) != (offset, size)
                or h[16:].split(b'\0', 1)[0] != name):
            raise ValueError('Device directory does not match the qualified layout')
    if any(b != 255 for b in view[RESERVED-BASE:START+LENGTH-BASE]):
        raise ValueError('Reclaimed extent contains data in the device acquisition')


def repack(encoded, plain, key, payload, cipher):
    if len(payload) != 0x165d40 or any(b != 255 for b in payload[APP_BYTES:]):
        raise ValueError('App does not fit the reduced allocation')
    if struct.unpack_from('<I', plain, BASE+8)[0] != OLD_END-BASE:
        raise ValueError('Unexpected old app area')
    p = bytearray(plain)
    p[BASE+0xc0:BASE+0xe0] = manifest()
    app = payload[:APP_BYTES]
    # Keep original flags, reserved bytes, final-entry index and name padding.
    struct.pack_into('<I', p, BASE+32+8, APP_BYTES)
    struct.pack_into('<H', p, BASE+32+2, crc_hqx(app, 0))
    struct.pack_into('<H', p, BASE+32, crc_hqx(p[BASE+34:BASE+64], 0))
    struct.pack_into('<I', p, BASE+8, AREA_BYTES)
    struct.pack_into('<H', p, BASE+2, crc_hqx(p[BASE+32:RESERVED], 0))
    struct.pack_into('<H', p, BASE, crc_hqx(p[BASE+2:BASE+32], 0))
    p[RESERVED:RESERVED+32] = header(b'CAPTURE', RESERVED, OLD_END-RESERVED, 0x12,
                                    data_crc=0xffff, reserved=0x81)
    out = bytearray(encoded)
    for off in (BASE, BASE+32, BASE+0xc0, RESERVED):
        block = bytearray(p[off:off+32])
        cipher(block, 0, 32, key ^ ((off-BASE) >> 2))
        out[off:off+32] = block
    if out[:BASE] != encoded[:BASE] or out[OLD_END:] != encoded[OLD_END:]:
        raise ValueError('Protected prefix/resources changed')
    return bytes(out), p, (BASE+0x100, RESERVED)


def verify_crcs_after_capture(plain):
    """Exercise BOTH boot/app checksum spans after changing every capture byte."""
    p = bytearray(plain)
    p[START:START+LENGTH] = b'\x5a' * LENGTH
    for at, data_start, end in ((BASE, BASE+32, RESERVED), (BASE+32, BASE+0x100, RESERVED)):
        if crc_hqx(p[at+2:at+32], 0) != struct.unpack_from('<H', p, at)[0]:
            raise ValueError('Header CRC mismatch')
        if crc_hqx(p[data_start:end], 0) != struct.unpack_from('<H', p, at+2)[0]:
            raise ValueError('Capture data intersects a boot/app CRC')
