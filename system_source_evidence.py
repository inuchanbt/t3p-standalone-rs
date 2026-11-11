"""Verify stock temperature and USB serial clues offline; no device access."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsp", type=Path, default=ROOT.parent / "doom_on_t3p")
    args = parser.parse_args()
    stock = args.bsp / "local/unpacked/T3P Firmware Version 3.0.3.bin"
    app = (stock / "files/app.bin").read_bytes()
    boot = (stock / "top/uboot.boot").read_bytes()
    if hashlib.sha256(app).hexdigest() != "ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c":
        raise ValueError("Unexpected stock app")
    if hashlib.sha256(boot).hexdigest() != "53b7b7f6ec1711becb538625a5afe4b45cc3dba5fafee8bcb229bd12d0791ec1":
        raise ValueError("Unexpected stock loader")
    pins = struct.unpack_from("<6I", app, 0xc111a10 - 0xc000100)
    if pins != (0, 6, 5, 0x11, 0, 100000): raise ValueError("Sensor bus configuration changed")
    evidence = []
    cases = [
        ("app-3.0.3", app, 0xc000100, 0, {
            0xc0e6d74: "r0 = 0xd0", 0xc0e6d7e: "r0 = 0xee",
            0xc0e6d8e: "if (r0 != 0x58)", 0xc0e6d96: "r0 = 0x88",
            0xc0e6dbe: "if (r5 != 0x18)", 0xc0e6de6: "r0 = 0xf4",
        }),
        ("uboot", boot, 0x102600, 16, {
            0x104ef0: "r9 = 0x109b40", 0x10530e: "r1 = r9 + 0xf4",
            0x105312: "r2 = b[r1+0x2]", 0x105314: "r0 = b[r1+0x3]",
            0x10531a: "r3 = b[r1+0x1]", 0x105320: "r1 = b[r1+0x0]",
            0x105326: "r2 = 0xf423f", 0x105332: "r1 = 0x186a0",
            0x10533e: "r8 = 0x22", 0x105348: "b[r10+0x1] = r2",
        }),
    ]
    for name, data, base, prefix, checks in cases:
        rows = {}
        for line in (args.bsp / f"analysis/{name}.disasm.txt").read_text(encoding="utf-8-sig").splitlines():
            match = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)", line)
            if match: rows[int(match[1], 16)] = (bytes.fromhex(match[2]), match[3].strip())
        for address, fragment in checks.items():
            raw, text = rows[address]
            offset = prefix + address - base
            if fragment not in text or data[offset:offset + len(raw)] != raw:
                raise ValueError(f"Source mismatch at {address:#x}")
            evidence.append({"source": name, "address": hex(address), "instruction": text, "bytes": raw.hex()})
    report = {"status": "Offline source checks only; physical temperature/serial acquisition unverified",
              "sensor_bus": "PA6 SCL, PA5 SDA", "sensor": "BMP280-compatible, I2C 0x77, ID 0x58",
              "serial_route": "Updater USB descriptor uses big-endian word at loader state + 0xf4; normal-boot ownership/validity unresolved",
              "serial_runtime_policy": "N/A; do not read unvalidated loader SRAM or hardcode the reported serial",
              "instructions": evidence}
    out = ROOT / "build/system/acquisition-evidence.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Verified sensor table and {len(evidence)} stock instructions: {out}")


if __name__ == "__main__":
    main()
