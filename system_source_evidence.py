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
    bmp_pins=struct.unpack_from("<6I",app,0xc11bcfc-0xc000100)
    if bmp_pins!=(0,18,17,0x11,0,400000): raise ValueError("Stock software I2C table changed")
    evidence = []
    cases = [
        ("app-3.0.3", app, 0xc000100, 0, {
            0xc0e6d74: "r0 = 0xd0", 0xc0e6d7e: "r0 = 0xee",
            0xc0e6d8e: "if (r0 != 0x58)", 0xc0e6d96: "r0 = 0x88",
            0xc0e6dbe: "if (r5 != 0x18)", 0xc0e6de6: "r0 = 0xf4",
            0xc0e6dc2: "r0 = 0xb6",0xc0e6dc8: "r0 = 0xe0",
            0xc0e6de0: "r0 = 0xb3",
            0xc0cefcc: "r1 = 0xc111a10",0xc0cf014:"r6 = 0xfd0c00",
            0xc0ea592: "r1 = 0xc11bcfc",
            0xc0ea59e: "r0 = 0x1",0xc0ea5a0:"r1 = 0x4",0xc0ea5a2:"r2 = 0x11",
            0xc0ea5aa: "r0 = 0x1",0xc0ea5ac:"r1 = 0x2",0xc0ea5ae:"r2 = 0x11",
            0xc0ea5d0: "r1 = 0x11dc28",0xc0ea5e8:"r4 = 0x11dbd4",
            0xc0ea5ee: "r1 = [r4+0x24]",0xc0ea608:"r1 = [r4+0x20]",
            0xc00ebe6: "r0 = 0x40",0xc00ec06:"r0 = 0x40",
            0xc0e6e1a: "r0 = 0x6",0xc0e6e1c:"r1 = 0x8",
            0xc0e6e2e: "r0 = 0x6",0xc0e6e30:"r1 = 0x80",
            0xc0e6e6a: "r0 = 0x2",0xc0e6e76:"r0 = 0x3",
            0xc0e6e82: "r0 = 0x4",0xc0e6e8e:"r0 = 0x5",
        }),
        ("uboot", boot, 0x102600, 16, {
            0x104ef0: "r9 = 0x109b40", 0x10530e: "r1 = r9 + 0xf4",
            0x105312: "r2 = b[r1+0x2]", 0x105314: "r0 = b[r1+0x3]",
            0x10531a: "r3 = b[r1+0x1]", 0x105320: "r1 = b[r1+0x0]",
            0x105326: "r2 = 0xf423f", 0x105332: "r1 = 0x186a0",
            0x10533e: "r8 = 0x22", 0x105348: "b[r10+0x1] = r2",
        }),
    ]
    call_edges={0xc0e6d84:0xc0cf502,0xc0cf51e:0xc0ea5d0,
                0xc0cf526:0xc0ea5e6,0xc0cf530:0xc0ea64c,
                0xc0cf53a:0xc0ea64c,0xc0cf542:0xc0ea5e6,
                0xc0cf54c:0xc0ea64c,0xc0cf558:0xc0ea84e,
                0xc0cf568:0xc0ea87e,0xc0ea5a4:0xc03505c,
                0xc0ea5b0:0xc03505c}
    secondary_calls={0xc0e6e1e:0xc00ebd6,0xc0e6e6e:0xc00ebf4,
                     0xc00ebea:0xc0cf57a,0xc00ec0a:0xc0cf502}
    edges=[];secondary_edges=[]
    for name, data, base, prefix, checks in cases:
        rows = {}
        for line in (args.bsp / f"analysis/{name}.disasm.txt").read_text(encoding="utf-8-sig").splitlines():
            match = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)", line)
            if match: rows[int(match[1], 16)] = (bytes.fromhex(match[2]), match[3].strip())
        if name=="app-3.0.3":
            for calls,destination in ((call_edges,edges),(secondary_calls,secondary_edges)):
                for address,expected in calls.items():
                    raw,text=rows[address]
                    delta=re.fullmatch(r"call (-?0x[0-9a-f]+)",text)
                    offset=address-base
                    if (delta is None or address+len(raw)+int(delta[1],16)!=expected or
                        data[offset:offset+len(raw)]!=raw):
                        raise ValueError(f"Sensor call route mismatch at {address:#x}: {text}")
                    destination.append(dict(caller=hex(address),callee=hex(expected),instruction=text,bytes=raw.hex()))
        for address, fragment in checks.items():
            raw, text = rows[address]
            offset = prefix + address - base
            if fragment not in text or data[offset:offset + len(raw)] != raw:
                raise ValueError(f"Source mismatch at {address:#x}")
            evidence.append({"source": name, "address": hex(address), "instruction": text, "bytes": raw.hex()})
    report = {"status": "Offline source checks only; physical temperature/serial acquisition unverified",
              "sensor_bus": "PB2 SCL, PB1 SDA (stock BMP ID call chain); shared meter bus",
              "sensor": "BMP280-compatible, I2C 0x77, ID 0x58",
              "bmp_software_i2c_table":dict(address="0xc11bcfc",words=bmp_pins,
                  init="0xc0ea578",state="0x11dc28",configuration="0x11dbf0",
                  mode="0x11: input with internal 10k pull-up",hdrive=0),
              "separate_hardware_i2c_table":dict(address="0xc111a10",words=pins,
                  controller="0xfd0c00",pins="PA6/PA5; not the BMP ID call route"),
              "correction":"Earlier SYSTEM evidence incorrectly associated the PA table with BMP. Trace the ID helper into PB software I2C instead.",
              "call_edges":edges,
              "secondary_sensor_route":dict(wire_address="40/41",address_7bit="20",
                  call_edges=secondary_edges,
                  inference="ENS220-compatible software fingerprint: address 20, MODE_CFG 06 reset/high-power sequence 08 then 80, UID 02..05; fitted sensor and response unverified",
                  source="https://www.sciosense.com/wp-content/uploads/2025/10/ENS220-Datasheet-1.pdf",
                  runtime_policy="V2 caches one PART_ID 00..01 read only; no ENS220 reset/configuration/conversion writes"),
              "serial_route": "Updater USB descriptor uses big-endian word at loader state + 0xf4; normal-boot ownership/validity unresolved",
              "serial_runtime_policy": "N/A; do not read unvalidated loader SRAM or hardcode the reported serial",
              "instructions": evidence}
    out = ROOT / "build/system/acquisition-evidence.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Verified two I2C tables, {len(evidence)} stock instructions, {len(edges)} BMP and {len(secondary_edges)} secondary call edges: {out}")


if __name__ == "__main__":
    main()
