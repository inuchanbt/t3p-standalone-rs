"""Verify the documented measurement-bus clues in the exact stock app."""

import argparse
import hashlib
from pathlib import Path
import struct

BASE = 0x0C000100
STOCK_SHA256 = "ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c"
STOCK_APP = (Path(__file__).resolve().parents[1] / "doom_on_t3p/local/unpacked"
             / "T3P Firmware Version 3.0.3.bin/files/app.bin")

EVIDENCE = {
    # Register number and call to c00eb6e, then the voltage scaling constant.
    0x0C0113BE: "40 22 bf ea d5 eb 3f e5 9f 00 c1 ff d0 53 d3 3a",
    # Register number, same reader, then signed low-halfword extraction.
    0x0C011448: "40 21 bf ea 90 eb 89 17",
    # Address 0x80, length 2, transfer call and high byte first.
    0x0C00EB7E: "60 20 43 22 42 16 80 ff 78 09 0c 00 48 40 49 41 a1 e1 60 04",
    # Write pointer, repeated START, read address.
    0x0C0CF52E: ("70 16 80 ff 16 b1 01 00 00 55 58 40 80 ff 0c b1 01 00 00 52 "
                 "80 ff 9e b0 01 00 40 e1 01 70 80 ff fa b0 01 00"),
    # Copy the software I2C config into RAM.
    0x0C0EA58C: "c0 ff f0 db 11 00 c1 ff fc bc 11 0c 10 86 12 05 82 05",
    0x0C1119C8: "f4 9b 15 0c",
    0x0C159BF4: b"fusb302_task\0".hex(),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", nargs="?", type=Path, default=STOCK_APP)
    app = parser.parse_args().app.read_bytes()
    if hashlib.sha256(app).hexdigest() != STOCK_SHA256:
        raise ValueError("Expected the unpacked T3P 3.0.3 stock app.bin")
    for address, expected in EVIDENCE.items():
        expected = bytes.fromhex(expected)
        offset = address - BASE
        if app[offset:offset + len(expected)] != expected:
            raise ValueError(f"Unexpected stock bytes at {address:#x}")
    bus = struct.unpack_from("<7I", app, 0x0C11BCFC - BASE)
    if bus != (0, 0x12, 0x11, 0x11, 0, 400000, 0x10000):
        raise ValueError("Unexpected software I2C configuration")
    print("Stock 3.0.3: PB2/PB1 software I2C, 0x40 registers 0x02/0x01, fusb302_task")


if __name__ == "__main__":
    main()
