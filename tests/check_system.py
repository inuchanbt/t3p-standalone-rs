"""Compile and run host C checks for SYSTEM and its measurement/PD paths."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang", help="Host C compiler")
    args = parser.parse_args()
    out = ROOT / "build/system-checks"
    out.mkdir(parents=True, exist_ok=True)
    features = ["-DT3P_MEASURE", "-DT3P_PD_PROFILES", "-DT3P_PD_CABLE_READ", "-DT3P_PD_CHARGER"]
    common = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-ffunction-sections",
              "-fdata-sections", "-Wl,--gc-sections"]
    cases = [
        ("system-clock", ["tests/system_clock.c"], []),
        ("system-input", ["tests/system_input.c"], []),
        ("measurement-system", ["tests/measurement.c", "native/t3p_meter.c"], features + ["-DT3P_SYSTEM"]),
        ("measurement-baseline", ["tests/measurement.c", "native/t3p_meter.c"], features),
        ("board-system", ["tests/board_capture_refresh.c"], ["-DT3P_SYSTEM"]),
        ("board-refresh", ["tests/board_capture_refresh.c"], []),
        ("board-key", ["tests/board_key.c"], []),
        # This existing test deliberately includes an unused static board entry.
        ("board-warning", ["tests/board_pd_warning.c"], ["-Wno-unused-function"]),
        ("charger-system", ["tests/pd_charger.c", "native/t3p_charger.c"], features + ["-DT3P_SYSTEM"]),
        ("cable-system", ["tests/pd_cable_read.c", "native/t3p_cable_read.c", "native/t3p_pd.c"], features + ["-DT3P_SYSTEM"]),
    ]
    for name, sources, flags in cases:
        binary = out / f"{name}.exe"
        subprocess.run([args.cc, *common, *flags, *sources, "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)
        print(f"{name}: PASS", flush=True)


if __name__ == "__main__":
    main()
