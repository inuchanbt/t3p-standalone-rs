"""Render the host frames used by check_pi32.py with an already built preview binary."""

import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", type=Path, default=ROOT / "target/debug/preview.exe")
    mode = parser.add_mutually_exclusive_group()
    for name in ("measurement", "pd-profiles", "pd-cable-read", "pd-charger",
                 "pd-rd-test", "pd-ack-test", "system", "capture-probe", "flash-reader"):
        mode.add_argument(f"--{name}", action="store_true")
    args = parser.parse_args()
    if args.capture_probe:
        args.system = True
    if args.system:
        args.pd_charger = True
    variant = next((name for name in ("flash-reader", "capture-probe", "system", "pd-charger", "pd-cable-read", "pd-profiles",
                                    "pd-ack-test", "pd-rd-test", "measurement")
                    if getattr(args, name.replace("-", "_"))), "")
    out = ROOT / "build" / variant
    out.mkdir(parents=True, exist_ok=True)
    profiles = args.pd_profiles or args.pd_cable_read or args.pd_charger
    common = ["--profiles-ui"] if profiles else []
    frames = {"preview": [], "demo-graph": ["--power"]}
    if args.flash_reader:
        frames['preview-flash-reader-ready']=['--flash-reader']
        frames['preview-flash-reader-locked']=['--flash-reader','--reader-locked']
        frames['preview-flash-reader-usb']=['--flash-reader','--reader-usb']
    if args.capture_probe:
        for section in range(4):
            frames[f"preview-capture-probe-{section}"] = [f"--capture-probe={section}"]
            frames[f"preview-capture-probe-missing-{section}"] = [f"--capture-probe={section}", "--probe-missing"]
    if variant:
        for name in ("measurement", "energy", "power", "voltage", "current", "missing"):
            frames[f"preview-{name}"] = ["--measurement"] + (
                [] if name == "measurement" else [f"--{name}"])
        frames["preview-long"] = ["--measurement", "--long-history", "--power", "--window10"]
    if profiles:
        groups = {
            "profiles": {"ready": "page", "listening": "listening", "offer": "offer"},
            "epr": {"ready": "page", "timeout": "timeout", "offer": "offer",
                    "partial": "partial", "diag": "diag"},
            "sniff": {"ready": "page", "log": "log", "old": "old", "alert": "alert"},
            "cable": {"ready": "page", "listening": "listening", "summary": "summary",
                      "raw": "raw", "flags": "flags", "timeout": "timeout"},
        }
        if args.pd_cable_read or args.pd_charger:
            groups["cable"].update({"vbus": "vbus", "no-ra": "no-ra"})
            for section in (0, 6, 8):
                frames[f"preview-cable-scan-{section}"] = ["--measurement", f"--cable-scan={section}"]
        if args.pd_charger:
            names = ("ready", "armed", "result", "offer", "identity", "peaks", "power",
                     "status", "limits", "info", "queries", "no-ext", "partial",
                     "unsupported", "unsupported-queries")
            groups["charger"] = {name: name for name in names}
        if args.system:
            groups["system"] = {name: name for name in (
                "info", "io", "input", "missing-info", "missing-io", "missing-input",
                "fault", "max-io", "boot-io", "temperature", "cold")}
        for group, states in groups.items():
            for name, flag in states.items():
                frames[f"preview-{group}-{name}"] = ["--measurement", f"--{group}-{flag}"]
    if args.pd_rd_test or args.pd_ack_test:
        for state in ("off", "5v"):
            frames[f"preview-rd-{state}"] = [f"--rd-{state}"]
    for name, flags in frames.items():
        subprocess.run([str(args.preview), str(out / f"{name}.ppm"), *common, *flags], check=True)
    # Measurement-only variants use this shared demo history reference.
    if variant and not profiles:
        subprocess.run([str(args.preview), str(ROOT / "build/demo-graph.ppm"), "--power"], check=True)
    print(f"Rendered {len(frames)} host reference frames in {out}")


if __name__ == "__main__":
    main()
