# T3P Standalone

[日本語](README.ja.md)

Experimental Rust firmware for the ATORCH T3P USB power meter, building on [KWS-X1 Standalone](https://github.com/kralonur/kws-x1-standalone-rs) and the local DOOM on T3P board research.

## Status

The current build is a **startup and display demo with simulated readings**. It displays voltage, signed current and power, charge and energy totals, and a 60-second power graph. PA11 resets the totals. Real measurement, USB PD and charger protocols remain to be implemented. The Rust demo has not yet been verified on a physical T3P.

The startup and display approach was previously tested on T3P hardware. The Pi32v2 machine code was compared with the PC renderer in an instruction simulator for one 62-second demo sequence. All 57,600 pixels matched. Physical behavior and other inputs need further verification.

## PC preview

Install Rust 2024 and Cargo, then run:

```sh
cargo test --offline
cargo run --offline --bin preview -- preview.ppm
```

The 240×240 PPM image contains simulated values.

## Experimental board build

The board build uses Rust 1.99.1 (`58cf8f9c3`), its `thumbv7em-none-eabihf` core, [Pi32v2 LLVM 23.1.5-2](https://github.com/jvitkauskas/pi32v2-llvm/releases/tag/pi32v2-23.1.5-2), and the JieLi assembler for the entry code. It retargets 32-bit LLVM IR to Pi32v2. This toolchain bridge is experimental. Active-time mode produced an incorrect Wh value in the simulator, so the demo uses elapsed-time mode.

Obtain and unpack your own T3P 3.0.3 firmware using [jl-misctools](https://github.com/kagaimiq/jl-misctools). The unpacked `app.bin` must have SHA-256 `ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c`. The build extracts the LCD initialization records locally into the ignored `build/` directory.

```sh
python build.py --rustc PATH_TO_RUSTC --llvm PATH_TO_PI32V2_LLVM --jieli-clang PATH_TO_JIELI_CLANG --stock-app PATH_TO_UNPACKED_APP_BIN
cargo run --offline --bin preview -- build/preview.ppm
python check_pi32.py --sim PATH_TO_PI32V2_ISA
```

The simulator check requires the pinned [pi32v2-isa revision](https://github.com/jvitkauskas/pi32v2-isa/tree/66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f). The output ELF is for offline evaluation. `pack_demo.py` currently depends on the separate local `doom_on_t3p` research workspace, unpacked stock firmware, and its packing tools. Manufacturer firmware and update binaries are not distributed here.

## Hardware research

Static analysis of the exact 3.0.3 application identifies a software I²C bus on PB2/PB1, address 0x40, and reads of registers 0x02 and 0x01. This resembles an INA226 register layout. Chip identification, physical wiring, calibration, and actual values need verification. `check_stock.py` checks the recorded byte evidence against a locally unpacked stock application.

## Attribution and license

`vendor/kws-x1/energy.rs` and `power_history.rs` derive from [KWS-X1 Standalone commit 6942fc6](https://github.com/kralonur/kws-x1-standalone-rs/tree/6942fc6052ac38e7afbd7d31a22ee8789f55c29a/firmware-core/src), used under the MIT option of its dual license. Their constructors were adapted for static initialization. The original MIT notice is in `vendor/kws-x1/LICENSE-MIT`.

The T3P startup and board code derives from the local DOOM on T3P research project. Project code is GPL-2.0-or-later; see [COPYING](COPYING) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This unofficial community project is not affiliated with ATORCH or the KWS-X1 authors.
