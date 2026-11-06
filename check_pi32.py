"""Compare the linked Pi32v2 renderer with the host preview, pixel for pixel."""

import argparse
import builtins
from importlib import import_module
from io import StringIO
import json
from pathlib import Path
import struct
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent
SIM_REVISION = "66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f"
SIDE = 240
STRIP_ROWS = 8
HEADER = b"P6\n240 240\n255\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sim", type=Path, default=ROOT / "local" / f"pi32v2-isa-{SIM_REVISION}")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--measurement", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.sim / "tools/llvm"))
    from sim import CPU

    out = args.build_dir
    expected = (ROOT / "build/preview.ppm").read_bytes()
    # The pinned simulator refers to an unpublished absolute path for its
    # memory-op IDs. Recover those IDs from its committed hardware fixtures.
    fixtures = ("followup5-m.json", "followup5-m-main.json")
    ids = {case["id"] for name in fixtures
           for case in json.loads((args.sim / "hw" / name).read_bytes())}
    hw_results = args.sim / "hw/fm1-2026-10-24-v2/results.jsonl"
    ids.update(json.loads(row)["id"] for row in hw_results.read_bytes().splitlines())
    for module in (args.sim / "tools/hwtest/semantics").glob("*.py"):
        if not module.name.startswith("_") and module.stem != "m2":
            ids.difference_update(import_module(f"semantics.{module.stem}").MODELS)
    real_open = builtins.open

    def open_model_ids(path, *args, **kwargs):
        if path == "/root/.t3/projects/pi32v2-isa-wt/ids5-m.txt":
            return StringIO("\n".join(sorted(ids)))
        return real_open(path, *args, **kwargs)

    with patch("builtins.open", open_model_ids):
        cpu = CPU()
    symbols = cpu.load_elf((out / "t3p-meter-demo.elf").read_bytes())
    cpu.sp = symbols["__runtime_stack_end"]
    strip = symbols["strip"]
    for tick in range(0, 62_001, 100):
        cpu.call(symbols["meter_demo_sample"], [tick])
    cpu.call(symbols["meter_snapshot"], [])
    def render():
        actual = bytearray(HEADER)
        for top in range(0, SIDE, STRIP_ROWS):
            cpu.call(symbols["meter_demo_render"], [top, strip])
            raw = cpu.mem.read(strip, SIDE * STRIP_ROWS * 2)
            for pixel, in struct.iter_unpack("<H", raw):
                actual.extend((((pixel >> 11) & 31) * 255 // 31,
                               ((pixel >> 5) & 63) * 255 // 63, (pixel & 31) * 255 // 31))
        return actual

    actual = render()
    if actual != expected:
        actual_path = out / "preview-pi32.ppm"
        actual_path.write_bytes(actual)
        raise ValueError(f"Pi32v2 demo frame differs from the host preview. See {actual_path}")
    print(f"Pi32v2: {SIDE * SIDE:,} RGB pixels match ({cpu.steps:,} simulated instructions)")
    if args.measurement:
        cpu.load_elf((out / "t3p-meter-demo.elf").read_bytes())
        cpu.sp = symbols["__runtime_stack_end"]
        for tick in range(0, 1001, 100):
            cpu.call(symbols["meter_live_sample"], [tick, 1, 5000, -2000])
        cpu.call(symbols["meter_snapshot"], [])
        if render() != (out / "preview-measurement.ppm").read_bytes():
            raise ValueError("Pi32v2 measurement input frame differs from host preview")
        cpu.call(symbols["meter_live_sample"], [1100, 0, 0, 0])
        cpu.call(symbols["meter_snapshot"], [])
        if render() != (out / "preview-missing.ppm").read_bytes():
            raise ValueError("Pi32v2 missing-reading frame differs from host preview")
        print("Pi32v2 measurement and missing-reading frames match")


if __name__ == "__main__":
    main()
