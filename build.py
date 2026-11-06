"""Compile and audit the T3P demo ELF offline."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
LLVM_VERSION = "23.1.5-2"
LLVM_REVISION = "38cdba719126c2a59995c16dd364d70c6561e86a"
RUST_VERSION = "1.99.1 (58cf8f9c3 2026-10-13)"
BSP_FILES = (
    "runtime_start.S", "runtime_memory.c", "t3p_signal.c", "t3p_boot.c",
    "t3p_bsp.c", "t3p_lcd_sequence.c", "t3p_bsp_native.c",
)


def run(*args, capture=False):
    result = subprocess.run(args, check=True, text=True, capture_output=capture)
    return result.stdout if capture else None


def retarget(ir):
    """Use Rust's portable integer IR with Pi32v2's machine-code backend."""
    arm_layout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
    if f'target datalayout = "{arm_layout}"' not in ir:
        raise ValueError("Rust input changed its data layout")
    if 'target triple = "thumbv7em-unknown-none-eabihf"' not in ir:
        raise ValueError("Unexpected Rust IR input target")
    if re.search(r"@llvm\.(arm|thumb)\.|\basm (?:sideeffect |alignstack )*\"", ir):
        raise ValueError("Target-specific code in Rust IR")
    ir = re.sub(r'^target (?:datalayout|triple) = .*\n', '', ir, flags=re.M)
    ir = re.sub(r' "(?:target-cpu|target-features|tune-cpu|target-abi)"="[^"]*"', '', ir)
    # rustc emits the crate's IR without core's panic-formatting functions.
    # Keep the same signatures and send their noreturn paths to the panic LED.
    panic_names = (
        "4core9panicking5panic", "4core9panicking9panic_fmt",
        "4core9panicking18panic_bounds_check", "4core5slice5index16slice_index_fail",
    )
    panic_decl = re.compile(r'^declare dso_local void @(_R\w+)\((.*)\) unnamed_addr #\d+$', re.M)

    def panic_body(match):
        name, params = match.groups()
        if not name.endswith(panic_names):
            raise ValueError(f"Unexpected Rust dependency {name}")
        return (f"define internal void @{name}({params}) noreturn {{\n"
                "  call void @t3p_signal_failure(i32 6)\n  unreachable\n}")

    ir, count = panic_decl.subn(panic_body, ir)
    if count:
        ir += "\ndeclare void @t3p_signal_failure(i32) noreturn\n"
    return ir


def generate_lcd_sequence(app, output):
    data = app.read_bytes()
    if hashlib.sha256(data).hexdigest() != "ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c":
        raise ValueError("Expected the unpacked stock 3.0.3 app.bin")
    def read(address, size):
        if 0x11f0e0 <= address and address + size <= 0x120c60:
            address = 0x0c161b80 + address - 0x11f0e0
        offset = address - 0x0c000100
        if offset < 0 or offset + size > len(data):
            raise ValueError("Stock address outside image")
        return data[offset:offset + size]
    import struct
    driver = struct.unpack("<14I", read(0x11f678, 56))
    stream = read(driver[2], driver[3])
    start, end = bytes.fromhex("12345678"), bytes.fromhex("87654321")
    records = []
    while stream:
        if not stream.startswith(start):
            raise ValueError("Invalid LCD record")
        stop = stream.index(end, len(start))
        records.append(stream[len(start):stop])
        stream = stream[stop + len(end):]
    if len(records) != 23 or not all(records):
        raise ValueError("Unexpected LCD records")
    lines = ['/* Generated locally from the stock LCD records. */',
             '#include "t3p_bsp.h"', 'int t3p_bsp_lcd_sequence(struct t3p_bsp *b)', '{']
    for index, record in enumerate(records):
        if record.startswith(bytes.fromhex("ff5aa5ff")):
            if len(record) != 5:
                raise ValueError("Invalid LCD delay")
            lines.append(f'    if(t3p_bsp_delay(b,{record[4]}))return -2;')
        else:
            if len(record) > 1:
                values = ",".join(hex(value) for value in record[1:])
                lines.append(f'    static const uint8_t data{index}[]={{' + values + '};')
            value = f'data{index}' if len(record) > 1 else '0'
            lines.append(f'    if(t3p_bsp_command(b,0x{record[0]:02x},{value},{len(record)-1}))return -2;')
    output.write_text("\n".join(lines + ['    return 0;', '}', '']), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsp", type=Path, default=ROOT.parent / "doom_on_t3p")
    parser.add_argument("--llvm", type=Path, default=ROOT / "local" / f"pi32v2-llvm-{LLVM_VERSION}-windows-x64")
    parser.add_argument("--rustc", type=Path, default=ROOT.parent / "doom_on_km003c" /
                        "tools/rustup/toolchains/stable-x86_64-pc-windows-gnu/bin/rustc.exe")
    parser.add_argument("--stock-app", type=Path, default=ROOT.parent / "doom_on_t3p/local/unpacked/T3P Firmware Version 3.0.3.bin/files/app.bin")
    parser.add_argument("--jieli-clang", type=Path)
    parser.add_argument("--measurement", action="store_true", help="Build the uncalibrated measurement candidate")
    parser.add_argument("--pd-diagnostic", action="store_true", help="Add read-only CC-controller diagnostics")
    args = parser.parse_args()
    if args.pd_diagnostic:
        args.measurement = True
    out = ROOT / "build" / ("pd-diagnostic" if args.pd_diagnostic else "measurement" if args.measurement else "")
    out.mkdir(parents=True, exist_ok=True)

    if run(args.rustc, "--version", capture=True).strip() != f"rustc {RUST_VERSION}":
        raise ValueError("Use the pinned Rust compiler for this experimental build")
    bins = args.llvm / "bin"
    clang = bins / "clang.exe"
    if LLVM_REVISION not in run(clang, "--version", capture=True):
        raise ValueError("Use the pinned Pi32v2 LLVM build")
    bsp = ROOT / "native"
    lcd_source = out / "t3p_lcd_sequence.c"
    generate_lcd_sequence(args.stock_app, lcd_source)
    common = ["--target=pi32v2", "-mcpu=r3"]

    rust_ir = out / "meter-thumb.ll"
    run(args.rustc, "--edition=2024", "--crate-type=staticlib", "--cfg", 'feature="board"',
        "--target=thumbv7em-none-eabihf", "-C", "opt-level=s", "-C", "panic=abort",
        "-C", "lto=fat", "--emit=llvm-ir", "-o", rust_ir, ROOT / "src/lib.rs")
    pi32_ir = out / "meter-pi32.ll"
    pi32_ir.write_text(retarget(rust_ir.read_text(encoding="utf-8")), encoding="utf-8")
    rust_object = out / "meter.o"
    run(clang, *common, "-Os", "-Wno-override-module", "-fstack-size-section", "-c",
        "-x", "ir", pi32_ir, "-o", rust_object)

    objects = [rust_object]
    sources = [*(lcd_source if name == "t3p_lcd_sequence.c" else bsp / name for name in BSP_FILES),
               bsp / "board.c", bsp / "memory.c"]
    if args.measurement:
        sources.append(bsp / "t3p_meter.c")
    for source in sources:
        obj = out / (source.name + ".o")
        if source.name == "runtime_start.S":
            # Keep the exact long-call encoding already audited on the T3P.
            jieli = args.jieli_clang or args.bsp / "local/tools/jieli/C$/JL/pi32/bin/clang.exe"
            run(jieli, *common, "-integrated-as", "-mllvm", "-pi32v2-large-program=true",
                "-x", "assembler", "-c", source, "-o", obj)
        else:
            flags = ["-DT3P_MEASURE"] if args.measurement else []
            if args.pd_diagnostic:
                flags.append("-DT3P_PD_DIAGNOSTIC")
            run(clang, *common, "-O2", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                "-ffunction-sections", "-fdata-sections", *flags, "-I", bsp, "-c", source, "-o", obj)
        objects.append(obj)

    runtime = args.llvm / "lib/clang/23/lib/pi32v2/libclang_rt.builtins.a"
    elf = out / "t3p-meter-demo.elf"
    keep_demo = ["--undefined=meter_demo_sample"] if args.measurement else []
    run(bins / "ld.lld.exe", "-T", bsp / "runtime_layout.ld", "--gc-sections", "--no-relax", *keep_demo,
        f"-Map={out / 't3p-meter-demo.map'}", "-o", elf, *objects, runtime)
    if run(bins / "llvm-nm.exe", "--undefined-only", elf, capture=True).strip():
        raise ValueError("Unresolved symbols in T3P demo")

    from runtime_layout_evidence import inspect_layout
    report = inspect_layout(elf)
    report["status"] = ("Pi32v2 ELF audited offline; uncalibrated measurement candidate"
                        if args.measurement else "Pi32v2 ELF audited offline; demo input only")
    if args.pd_diagnostic:
        report["status"] += "; read-only PD diagnostic"
    report["rust"] = RUST_VERSION
    report["llvm"] = LLVM_VERSION
    report["elf_sha256"] = hashlib.sha256(elf.read_bytes()).hexdigest()
    (out / "audit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{elf}\nStatic RAM {report['actual_static_ram_bytes']:,} bytes")


if __name__ == "__main__":
    main()
