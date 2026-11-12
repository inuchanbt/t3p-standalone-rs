"""Verify local stock/SDK evidence for the read-only capture preflight.

No USB/device access and no firmware or physical flash modification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parent
APP_SHA = "ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c"
CPU_SHA = "787b6fc0913a0a8a634ee8c7606d5e4a68dc5604d8feacf806fd60dc1e2fc8ca"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsp", type=Path, default=ROOT.parent / "doom_on_t3p")
    args = parser.parse_args()
    app = (args.bsp / "local/unpacked/T3P Firmware Version 3.0.3.bin/files/app.bin").read_bytes()
    sdk = args.bsp / "local/jieli-sdk/SDK"
    cpu = (sdk / "cpu/br35/liba/cpu.a").read_bytes()
    if hashlib.sha256(app).hexdigest() != APP_SHA or hashlib.sha256(cpu).hexdigest() != CPU_SHA:
        raise ValueError("Stock app or pinned SDK archive changed")
    evidence = [
        (0xc00011a, "80 ff 44 54 10 00", "call the boot-copy helper"),
        (0xc105564, "50 ec 04 20", "load root words 1 and 2"),
        (0xc105568, "c1 ff 60 05 10 00", "destination is boot state at 0x100560"),
        (0xc10556e, "50 ec 11 21", "copy those two root words"),
        (0xc105572, "02 60", "load pointer from root word 0"),
        (0xc105574, "23 62", "load pointed-to header offset 8"),
        (0xc105578, "2a 4d", "load pointed-to header byte 13"),
        (0xc105584, "50 ec 00 21", "load root words 4 and 5"),
        (0xc033d62, "c0 ff 00 02 fe 00", "stock access to SFC register base 0xfe0200"),
    ]
    for address, encoded, _ in evidence:
        raw = bytes.fromhex(encoded)
        offset = address - 0xc000100
        if app[offset:offset + len(raw)] != raw:
            raise ValueError(f"Instruction bytes changed at {address:08x}")
    call_offset = 0xc00011a - 0xc000100
    displacement = struct.unpack_from("<i", app, call_offset + 2)[0]
    assert 0xc00011a + 6 + displacement == 0xc105564
    header = (sdk / "interface/driver/cpu/br35/asm/br35.h").read_text(encoding="utf-8")
    body = header.split("//............. 0x0200 - 0x02ff", 1)[1].split("} JL_SFC_TypeDef;", 1)[0]
    fields = re.findall(r"(__RW|__WO)\s+__u32\s+(\w+);", body)
    assert fields == [("__RW", name) for name in ("CON0", "CON1", "USER_CMD", "USER_CRM", "OFFSET_ADR")] + [
        ("__WO", "DEC_KEY")] + [("__RW", name) for name in ("NDEC_SADR", "NDEC_EADR", "CLK_CON", "DTR_CON")]
    reads = {fields[i][1]: hex(0xfe0200 + i * 4) for i in (0, 1, 4, 6, 7, 8, 9)}
    report = dict(status="OFFLINE SOURCE EVIDENCE; physical flash layout and capture backend NOT VERIFIED",
                  stock_app_sha256=APP_SHA, sdk_cpu_archive_sha256=CPU_SHA,
                  instructions=[dict(address=hex(a), bytes=b, meaning=m) for a, b, m in evidence],
                  probe_read_registers=reads, write_only_key_excluded=True,
                  root0_header_sram_copy_bytes=32, raw_words_in_probe=36,
                  flash_write_enabled=False, usb_export_enabled=False)
    out = ROOT / "build/capture-probe/source-evidence.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Verified 9 stock instructions, the boot-copy call target and 7 read-only probe register selections")
    print(out)


if __name__ == "__main__":
    main()
