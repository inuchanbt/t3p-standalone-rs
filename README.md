# T3P Standalone

[日本語](README.ja.md)

Experimental Rust firmware for the ATORCH T3P USB power meter, building on [KWS-X1 Standalone](https://github.com/kralonur/kws-x1-standalone-rs) and the local DOOM on T3P board research.

## Status

The default build is a **startup and display demo with simulated readings**. It displays voltage, signed current and power, charge and energy totals, and a 60-second power graph. A PA11 total reset and an experimental, uncalibrated measurement build are implemented. USB PD and charger protocols remain to be implemented.

The overview also shows `T HH:MM:SS`, elapsed time since startup or the last total reset, counting even when the reading is missing or current is zero. A left-button press resets the time together with Ah and Wh. The display stops at `99:59:59`; totals and elapsed time are not retained on restart. The separate PD-diagnostic build uses this area for its diagnostic line. The elapsed-time display still needs an on-device test.

On 2026-11-06 the user reported flashing the demo to a T3P and supplied photographs showing the demo screen, two different simulated readings, changing totals and graph history. The reported test used the prepared demo image, whose local SHA-256 is `b557f5fd89697917a11d07538279e0a6b78f83325ad2aa23b85af3db35f4f0ef`. The photographs do not verify the button, exact timing, cold boot, or real measurement.

The user subsequently reported difficulty seeing the zero totals. In a later video, both Ah and Wh are visibly `0.000` after the button press, then begin increasing again. The video does not establish which image revision was installed. The current code polls the button between display strips and requests a fresh snapshot after a detected reset; that specific revision still needs confirmation on the device.

The Pi32v2 machine code was also compared with the PC renderer in an instruction simulator for one 62-second demo sequence. All 57,600 pixels matched. Other inputs need further verification.

An optional experimental measurement build reads the monitor at 0x40 over the PB2/PB1 software I²C bus. It checks the stock firmware's die ID `0x2260` and continuous-conversion mode before reading bus and shunt registers. It displays nominal, **uncalibrated** values under `UNCAL`, and shows `---` if identification or communication fails. It does not load per-unit calibration. Polarity, conversion accuracy and timing remain unverified.

On 2026-11-06, the user reported a first hardware test of the measurement candidate. Photographs show 3.008 V with no measurement cable, 4.991 V with the cable in the reported CC2 orientation, and 1.839 V in the reported CC1 orientation. The source was the C1 port of an AOHI AOC-C022. Current was usually displayed as zero, occasionally reaching −0.001 A. The actual VBUS at the T3P has not yet been independently measured in either orientation. The cable-orientation difference and open-port reading remain under investigation, and the values have not been calibrated against a reference.

The source's ASD-PD31 log reported a 5 V / 3 A fixed PDO after one empty PDO result and a retry. It does not establish the voltage delivered in the T3P test. The stock application accesses 7-bit I²C address 0x22 through the same bus as the meter and has a `fusb302_task`; actual chip identity remains to be confirmed. The optional `--pd-diagnostic` build adds read-only snapshots of registers 0x01, 0x02, 0x08, 0x0B and 0x40. It does not configure CC, request PD contracts, or clear interrupt registers.

The diagnostic build was subsequently tested on the device: all three photographs showed `PD 91 03 02 01 01`, with displayed voltages of 3.010 V (no measurement cable), 4.994 V and 1.865 V (the two cable orientations). The ID and register defaults are consistent with an FUSB302B-compatible device; identification by marking or further evidence is still pending. If the register map matches FUSB302B, `SWITCHES0=03` enables both CC pull-downs but selects neither CC for measurement, so the unchanged `STATUS0` does not establish either CC voltage. The T3P CC topology remains to be independently verified.

In a later test with a USB-C breakout inserted between the source cable and T3P, the user measured 4.992 V in the reported CC2 orientation and approximately 1.733 V in CC1, matching the T3P display to three decimal places. In CC1, the measured voltage started above 2 V and decreased over time. These observations support the displayed voltage at those two test points, but do not establish its accuracy over the full range or explain the low-voltage condition. The breakout can affect the connection, so the test is not identical to the earlier direct-cable photographs.

With a Framework FRANDVCH00 source, Sumitomo Electric DC-010338 cable, and ASD-PD31 load, photographs show current readings of 1.002 / 1.000 A, 1.500 / 1.500 A, and 3.007 / 3.000 A (T3P / ASD-PD31) at a fixed 5 V request. At 20 V and 48 V fixed requests, the readings were 5.011 / 5.000 A and 5.012 / 5.000 A; the 48 V photograph shows 47.435 V and 237.754 W on T3P. HID was powered through an isolator connected to a PC. These are comparisons with another instrument, not calibration; voltage measurement points can differ. The ASD-PD31 requests the PD contracts. Accumulation timing and energy accuracy remain unverified on hardware.

In an approximately 8 min 35 s continuous 1 A test, T3P displayed 0.143 Ah and 0.724 Wh; the photographed instantaneous values were 1.001 A and 5.069 W. The result is consistent with the approximate duration and load, although the initial timing, load variations, display refresh and calibration limit precision. The graph now uses a minimum 1 W full-scale so milliwatt-level noise does not fill it; numeric readings and integration are unchanged.

The user later tested the graph change on the device: photographs show low bars with no load and a rising graph at approximately 1 A. The photograph of the latter shows 1.000 A on both T3P and ASD-PD31.

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

For the measurement candidate, add `--measurement` to `build.py`. Generate host references with `cargo run --offline --bin preview -- build/measurement/preview-measurement.ppm --measurement` and `cargo run --offline --bin preview -- build/measurement/preview-missing.ppm --measurement --missing`, then run `python check_pi32.py --build-dir build/measurement --measurement --sim PATH_TO_PI32V2_ISA`.

For the optional PD diagnostic, build with `python build.py --pd-diagnostic`. Generate the same two references under `build/pd-diagnostic`, plus `cargo run --offline --bin preview -- build/pd-diagnostic/preview-pd.ppm --measurement --missing --pd`; check them with `python check_pi32.py --build-dir build/pd-diagnostic --measurement --pd-diagnostic --sim PATH_TO_PI32V2_ISA`. The five displayed bytes are registers 0x01, 0x02, 0x08, 0x0B and 0x40 in that order.

The simulator check requires the pinned [pi32v2-isa revision](https://github.com/jvitkauskas/pi32v2-isa/tree/66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f). The output ELF is for offline evaluation. `pack_demo.py` currently depends on the separate local `doom_on_t3p` research workspace, unpacked stock firmware, and its packing tools. Manufacturer firmware and update binaries are not distributed here.

## Hardware research

Static analysis of the exact 3.0.3 application identifies a software I²C bus on PB2/PB1, address 0x40, and reads of registers 0x02 and 0x01. This resembles an INA226 register layout. Chip identification, physical wiring, calibration, and actual values need verification. `check_stock.py` checks the recorded byte evidence against a locally unpacked stock application.

## Attribution and license

`vendor/kws-x1/energy.rs` and `power_history.rs` derive from [KWS-X1 Standalone commit 6942fc6](https://github.com/kralonur/kws-x1-standalone-rs/tree/6942fc6052ac38e7afbd7d31a22ee8789f55c29a/firmware-core/src), used under the MIT option of its dual license. Their constructors were adapted for static initialization. The original MIT notice is in `vendor/kws-x1/LICENSE-MIT`.

The T3P startup and board code derives from the local DOOM on T3P research project. Project code is GPL-2.0-or-later; see [COPYING](COPYING) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This unofficial community project is not affiliated with ATORCH or the KWS-X1 authors.
