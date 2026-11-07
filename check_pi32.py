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
    parser.add_argument("--pd-diagnostic", action="store_true")
    parser.add_argument("--pd-profiles", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.sim / "tools/llvm"))
    from sim import CPU, Memory

    class T3PFlashMemory(Memory):
        def read(self, address, count):
            # The FM-1 instruction model keeps RETS bits 26..1. Let those
            # modeled return addresses fetch the same linked T3P flash bytes.
            if 0x04000100 <= address < 0x04165E40:
                address += 0x08000000
            return super().read(address, count)

    out = args.build_dir
    shared = out if args.pd_profiles else ROOT / "build"
    expected = (shared / "preview.ppm").read_bytes()
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
    cpu.mem = T3PFlashMemory()
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
        # Exercise the graph with a full minute of changing readings and gaps,
        # rather than only the single point in the short measurement preview.
        cpu.call(symbols["meter_next_page"], [])
        cpu.call(symbols["meter_next_page"], [])
        cpu.call(symbols["meter_snapshot"], [])
        if render() != (shared / "demo-graph.ppm").read_bytes():
            raise ValueError("Pi32v2 full-history graph differs from host preview")
        print("Pi32v2 full-history graph frame matches")
        cpu.load_elf((out / "t3p-meter-demo.elf").read_bytes())
        cpu.sp = symbols["__runtime_stack_end"]
        for tick in range(0, 1001, 100):
            cpu.call(symbols["meter_live_sample"], [tick, 1, 5000, -2000])
        cpu.call(symbols["meter_snapshot"], [])
        if render() != (out / "preview-measurement.ppm").read_bytes():
            raise ValueError("Pi32v2 measurement input frame differs from host preview")
        cpu.call(symbols["meter_double_press"], [])
        cpu.call(symbols["meter_snapshot"], [])
        if render() != (out / "preview-energy.ppm").read_bytes():
            raise ValueError("Pi32v2 energy frame or non-graph double press differs from host preview")
        for name in ("power", "voltage", "current"):
            cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / f"preview-{name}.ppm").read_bytes():
                raise ValueError(f"Pi32v2 {name} graph differs from host preview")
        cpu.call(symbols["meter_next_page"], [])
        if args.pd_profiles:
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-profiles-ready.ppm").read_bytes():
                raise ValueError("Pi32v2 initial PD profiles frame differs from host preview")
            if cpu.call(symbols["meter_double_press"], [])[0] != 1:
                raise ValueError("Pi32v2 PD profiles double press did not request capture")
            frame = bytes.fromhex((ROOT / "tests/pd_offer.hex").read_text(encoding="ascii"))
            cpu.call(symbols["meter_pd_status"], [1, 1, 0])
            cpu.mem.write(strip, frame[:-1] + bytes([frame[-1] ^ 1]))
            if cpu.call(symbols["meter_pd_record"], [strip, len(frame), 0, 0])[0] != 0xffffffff:
                raise ValueError("Pi32v2 accepted a corrupt PD frame")
            cpu.mem.write(strip, frame)
            if cpu.call(symbols["meter_pd_record"], [strip, len(frame), 0, 0])[0] != 1:
                raise ValueError("Pi32v2 rejected the test source offer")
            cpu.call(symbols["meter_pd_status"], [2, 1, 0])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-profiles-offer.ppm").read_bytes():
                raise ValueError("Pi32v2 captured PD profiles frame differs from host preview")
            print("Pi32v2 PD profiles and source-offer decoding match")
            cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-epr-ready.ppm").read_bytes():
                raise ValueError("Pi32v2 initial EPR frame differs from host preview")
            if cpu.call(symbols["meter_double_press"], [])[0] != 2:
                raise ValueError("Pi32v2 EPR double press did not request EPR capture")
            chunks = [bytes.fromhex((ROOT / "tests" / f"epr_chunk{i}.hex").read_text(encoding="ascii"))
                      for i in range(2)]
            cpu.call(symbols["meter_pd_status"], [1, 1, 1])
            cpu.mem.write(strip, frame)
            if cpu.call(symbols["meter_pd_record"], [strip, len(frame), 1, 90])[0] != 0:
                raise ValueError("Pi32v2 EPR listener did not ignore the SPR source offer")
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-epr-diag.ppm").read_bytes():
                raise ValueError("Pi32v2 EPR diagnostic counters differ from host preview")
            cpu.call(symbols["meter_pd_status"], [1, 1, 1])
            for chunk, now, expected_result in zip(chunks, (100, 120), (3, 1)):
                cpu.mem.write(strip, chunk)
                if cpu.call(symbols["meter_pd_record"], [strip, len(chunk), 1, now])[0] != expected_result:
                    raise ValueError("Pi32v2 failed to decode an EPR chunk")
            cpu.call(symbols["meter_pd_status"], [2, 1, 1])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-epr-offer.ppm").read_bytes():
                raise ValueError("Pi32v2 EPR offer frame differs from host preview")
            cpu.call(symbols["meter_pd_status"], [1, 1, 1])
            cpu.mem.write(strip, chunks[1])
            if cpu.call(symbols["meter_pd_record"], [strip, len(chunks[1]), 1, 120])[0] != 2:
                raise ValueError("Pi32v2 did not identify the partial EPR offer")
            cpu.call(symbols["meter_pd_status"], [10, 1, 1])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-epr-partial.ppm").read_bytes():
                raise ValueError("Pi32v2 partial EPR frame differs from host preview")
            print("Pi32v2 complete and partial EPR offers match")
            cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-sniff-ready.ppm").read_bytes():
                raise ValueError("Pi32v2 initial PD sniff frame differs from host preview")
            if cpu.call(symbols["meter_double_press"], [])[0] != 3:
                raise ValueError("Pi32v2 PD sniff double press did not request capture")
            cpu.call(symbols["meter_pd_status"], [1, 1, 2])
            exchange = [frame] + [bytes.fromhex(line) for name in ("sniff_exchange.hex", "sniff_epr.hex")
                                  for line in (ROOT / "tests" / name).read_text(encoding="ascii").splitlines()]
            for packet in exchange:
                cpu.mem.write(strip, packet)
                if cpu.call(symbols["meter_pd_record"], [strip, len(packet), 2, 0])[0] != 0:
                    raise ValueError("Pi32v2 failed to log a PD sniff packet")
            cpu.call(symbols["meter_pd_status"], [3, 1, 2])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-sniff-log.ppm").read_bytes():
                raise ValueError("Pi32v2 PD sniff log differs from host preview")
            cpu.call(symbols["meter_pd_status"], [6, 1, 2])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-sniff-alert.ppm").read_bytes():
                raise ValueError("Pi32v2 FIFO alert frame differs from host preview")
            cpu.call(symbols["meter_pd_status"], [3, 1, 2])
            cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-sniff-old.ppm").read_bytes():
                raise ValueError("Pi32v2 older PD sniff history differs from host preview")
            cpu.call(symbols["meter_pd_status"], [0, 0, 0])
            cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-measurement.ppm").read_bytes():
                raise ValueError("Pi32v2 did not advance after the oldest sniff page")
            for _ in range(7):
                cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_next_page"], [])
            if cpu.call(symbols["meter_double_press"], [])[0] != 3:
                raise ValueError("Pi32v2 sniff recapture was not available while browsing")
            cpu.call(symbols["meter_pd_status"], [1, 1, 2])
            cpu.mem.write(strip, exchange[1])
            cpu.call(symbols["meter_pd_record"], [strip, len(exchange[1]), 2, 0])
            cpu.call(symbols["meter_pd_status"], [3, 1, 2])
            cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_pd_status"], [0, 0, 0])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-measurement.ppm").read_bytes():
                raise ValueError("Pi32v2 sniff recapture did not reset the history cursor")
            for _ in range(7):
                cpu.call(symbols["meter_next_page"], [])
            cpu.call(symbols["meter_long_press"], [])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-measurement.ppm").read_bytes():
                raise ValueError("Pi32v2 PD sniff long press did not exit without resetting readings")
            print("Pi32v2 PD sniff EPR actions, older history and long-press exit match")
            cpu.call(symbols["meter_pd_status"], [0, 0, 0])
        cpu.call(symbols["meter_live_sample"], [1100, 0, 0, 0])
        cpu.call(symbols["meter_snapshot"], [])
        if render() != (out / "preview-missing.ppm").read_bytes():
            raise ValueError("Pi32v2 missing-reading frame differs from host preview")
        print("Pi32v2 overview, energy, power, voltage, current and missing-reading frames match")
        if args.pd_diagnostic:
            cpu.call(symbols["meter_pd_diagnostic"], [1, 0x04030291, 0x05])
            cpu.call(symbols["meter_snapshot"], [])
            if render() != (out / "preview-pd.ppm").read_bytes():
                raise ValueError("Pi32v2 PD diagnostic frame differs from host preview")
            print("Pi32v2 read-only PD diagnostic frame matches")
        cpu.load_elf((out / "t3p-meter-demo.elf").read_bytes())
        cpu.sp = symbols["__runtime_stack_end"]
        for second in range(601):
            valid = not 300 <= second < 310
            current = 1000 if second < 300 else 2000
            cpu.call(symbols["meter_live_sample"], [second * 1000, int(valid), 5000, current])
        cpu.call(symbols["meter_next_page"], [])
        cpu.call(symbols["meter_next_page"], [])
        for _ in range(3):
            cpu.call(symbols["meter_double_press"], [])
        cpu.call(symbols["meter_snapshot"], [])
        if render() != (out / "preview-long.ppm").read_bytes():
            raise ValueError("Pi32v2 ten-minute history differs from host preview")
        print("Pi32v2 ten-minute graph, gaps and range switching match")


if __name__ == "__main__":
    main()
