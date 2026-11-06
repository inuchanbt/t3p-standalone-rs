"""Package the audited T3P demo as a 3.0.3-format update, offline."""

import argparse
from binascii import crc_hqx
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parent
STOCK_NAME = "T3P Firmware Version 3.0.3.bin"
STOCK_SHA = "72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2"
IMAGE_NAME = "T3P_RUST_METER_DEMO_3.0.3.bin"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_files(directory):
    return {p.relative_to(directory): sha(p.read_bytes())
            for p in directory.rglob("*") if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsp", type=Path, default=ROOT.parent / "doom_on_t3p")
    parser.add_argument("--sim", type=Path)
    parser.add_argument("--output-name", default=IMAGE_NAME, help="Output filename within build/")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    if Path(args.output_name).name != args.output_name or not args.output_name.lower().endswith(".bin"):
        parser.error("--output-name must be a .bin filename")

    sys.path.insert(0, str(args.bsp / "scripts"))
    from audit_stock_recovery import entry, require
    from build_native_board import flatten
    from check_stock_app_container import rebuild_in_memory
    from runtime_layout_evidence import inspect_layout

    out = args.build_dir
    elf = out / "t3p-meter-demo.elf"
    elf_sha = sha(elf.read_bytes())
    build_audit = json.loads((out / "audit.json").read_bytes())
    require(elf_sha == build_audit["elf_sha256"], "ELF does not match build audit")
    inspect_layout(elf)
    check = [sys.executable, ROOT / "check_pi32.py", "--build-dir", out]
    if "measurement candidate" in build_audit["status"]:
        check.append("--measurement")
    if args.sim:
        check += ["--sim", args.sim]
    subprocess.run(check, check=True)
    payload, span = flatten(elf)
    require(sha(elf.read_bytes()) == elf_sha, "ELF changed during packaging")

    stock = (args.bsp / "artifacts/stock-restore-3.0.3" / STOCK_NAME).read_bytes()
    require(sha(stock) == STOCK_SHA, "Unexpected stock recovery image")
    extracted = args.bsp / "local/unpacked" / STOCK_NAME
    plain = (extracted / "decrypted.bin").read_bytes()
    stock_app = (extracted / "files/app.bin").read_bytes()
    key = json.loads((extracted / "jlfw.yaml").read_bytes())["chip-key"]
    original, _, _ = rebuild_in_memory(stock, plain, key, stock_app)
    require(original == stock, "Stock no-op repackage failed")

    encoded, expected_plain, (app_start, app_end) = rebuild_in_memory(stock, plain, key, payload)
    require(len(encoded) == len(stock), "Container length changed")
    unpacker = args.bsp / "vendor/jl-misctools/firmware/fwunpack_newfw.py"
    with TemporaryDirectory(dir=out) as scratch:
        staging = Path(scratch) / args.output_name
        staging.write_bytes(encoded)
        unpacked = Path(scratch) / "unpacked"
        result = subprocess.run([sys.executable, unpacker, "--dirname", unpacked, staging],
                                check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        decoded = (unpacked / "decrypted.bin").read_bytes()
        require(decoded == expected_plain, "Independent unpacker disagrees with packed image")
        area, app = entry(decoded, 0x5000), entry(decoded, 0x5020)
        require(crc_hqx(decoded[0x5020:0x5000 + area["size"]], 0) == area["data_crc"],
                "App-area data CRC mismatch")
        require(crc_hqx(decoded[app_start:app_end], 0) == app["data_crc"],
                "App data CRC mismatch")
        require((unpacked / "files/app.bin").read_bytes() == payload, "Unpacked payload mismatch")
        others = read_files(extracted / "files")
        updated = read_files(unpacked / "files")
        for files in (others, updated):
            del files[Path("app.bin")]
        require(others == updated, "Another unpacked file changed")
        require(read_files(extracted / "top") == read_files(unpacked / "top"),
                "An unpacked boot file changed")

        staging.replace(out / args.output_name)

    report = {
        "status": "Candidate packaged and audited offline; on-device execution pending",
        "image": args.output_name,
        "image_sha256": sha(encoded),
        "image_bytes": len(encoded),
        "stock_sha256": STOCK_SHA,
        "elf_sha256": elf_sha,
        "payload_sha256": sha(payload),
        "payload_span_bytes": span,
        "application_range": [hex(app_start), hex(app_end)],
        "boot_prefix_bytes_unchanged": 0x5000,
        "non_application_files_matched": len(others),
        "full_decrypt_matches": True,
        "nested_crcs_checked": 4,
        "simulated_rgb_pixels_matched": 57600,
        "demo_input_only": "demo input only" in build_audit["status"],
        "measurement_input_simulated": "measurement candidate" in build_audit["status"],
    }
    report_name = "demo-update" if args.output_name == IMAGE_NAME else Path(args.output_name).stem
    (out / f"{report_name}-unpack.log").write_bytes(result.stdout)
    (out / f"{report_name}-audit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{out / args.output_name}\nSHA-256 {report['image_sha256']}")


if __name__ == "__main__":
    main()
