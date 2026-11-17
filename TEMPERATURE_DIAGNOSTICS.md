# Temperature diagnostics V1

**Physical follow-up:** V1 reached the ID phase but did not acquire BMP ID=58.
The last transaction was a PB write-address NACK. See
[TEMPERATURE_PROBE_V2.md](TEMPERATURE_PROBE_V2.md) for the preserved physical
record, limits of the historical counters, and the next finite probe candidate.

The separate `temperature-diagnostics` candidate retains the completed Capture
V5 storage path and corrects the BMP transport to **PB2 SCL / PB1 SDA**. It records
acquisition phases, transport failures, ACKs and GPIO observations. Physical
temperature acquisition is **not yet verified**. Unread, failed and stale values
remain N/A. The released V5 binary is not replaced by this candidate.

## Stock route correction (2026-11-16)

The handoff's PA6/PA5 association was incorrect. Checking a GPIO table alone did
not establish which I2C implementation the BMP call actually used.

| Stock evidence | Meaning |
| --- | --- |
| `0C0E6D74..0C0E6D8E` | ID register D0, wire address EE, expected ID 58 |
| Call at `0C0E6D84` -> `0C0CF502` | BMP's register-read wrapper |
| `0C0CF51E` -> `0C0EA5D0` | Software-I2C ownership state at `0011DC28` |
| `0C0CF526` and `0C0CF542` -> `0C0EA5E6` | Initial/repeated START |
| `0C0CF530`, `0C0CF53A`, `0C0CF54C` -> `0C0EA64C` | Address, register and read-address transmission |
| `0C0CF558` -> `0C0EA84E`; `0C0CF568` -> `0C0EA87E` | Read buffer and STOP |
| Initialization `0C0EA578`, table `0C11BCFC` | `(0,18,17,0x11,0,400000)`; PB2/PB1, internal 10k pull-up, drive setting 0 |
| `0C0EA59E..0C0EA5B4` | Explicit port 1, masks 4 and 2, GPIO mode 11; both GPIO configuration calls resolve to `0C03505C` |
| Separate table `0C111A10` | PA6/PA5, 100 kHz; hardware controller at FD0C00, not the BMP ID route |

The START routine accesses `0011DBD4 + 20/24/28`, i.e. the SCL/SDA/mode
fields of the software table cached at `0011DBF0`. The table's GPIO numbers
18/17 decimal are hexadecimal 12/11, or PB2/PB1; they are not PA18/PA17.

`system_source_evidence.py` checks the original app/loader hashes, both tables,
42 selected instructions, 11 BMP and four secondary sensor call targets against their original
instruction bytes. Its report is `build/system/acquisition-evidence.json`.
These establish a software routing error, not the physical chip's identity or
proof that changing this route alone will yield a valid temperature.

| Item | Completed V5 | Diagnostic candidate |
| --- | --- | --- |
| Temperature pins | PA6/PA5 (incorrect stock association) | Existing meter PB2/PB1 bus |
| PA writes during temperature initialization | DIR/OUT/DIE bits 5/6 | None; before/after observations only |
| PB initialization | Meter GPIO setup | Same existing setup; no extra PB configuration |
| Bus delay | Existing TIMER1, 5 us wait | Same timer/delay; stock table's 400 kHz is not claimed as measured speed |
| ID/calibration | D0=58, six temperature calibration bytes | Same; all observations have validity flags |
| Conversion | F4=21, temperature x1, pressure skipped, forced | Same; no new reset/pressure writes |
| Stock differences after ID | Stock reads 24 calibration bytes, E0=B6, F4=B3, F5=14 | Intentionally retain temperature-only conversion until the bus trial is verified |
| Capture | V5 verified-blank handoff, streamed blank check, page buffering | Same storage implementation and three optimizations |

## Screens and cached USB record

The candidate identifies as `T3P Temp Diag V1`; SYSTEM APP is `T3P TEMP DIAG V1`.
SYSTEM has six sections. The first three retain the existing information.

| Section | Display |
| --- | --- |
| 4/6 TEMP ACQUISITION | Attempts, failures, phase/result, register and bus stage, ACK validity/success masks, observed ID/status, raw ADC, scheduling gate and timer readiness |
| 5/6 TEMP PB2/PB1 R/F | Active bus: released versus failure-before-STOP; unavailable failure samples are `--` |
| 6/6 TEMP PA5/PA6 B/A | PA observations before/after temperature initialization; no PA writes in the candidate |

GPIO screen values are masked to the two named pins: PA mask 60, PB mask 06.
DIR=1 means input/released; OUT is the output latch, not a measurement of line
voltage. IN reports sampled digital levels. DIE/DIEH, PU0/PU1, PD0/PD1,
HD0/HD1, SPL and CON are available. Full register words and post-STOP PB state
are in the USB record. Observations are sequential CPU reads, not simultaneous
waveforms or a calibrated voltage measurement. Per-bit instrumentation stores
only the bit position; GPIO sampling occurs at release/failure/STOP boundaries.
There is no drawing, allocation, console output or USB transfer inside I2C.

Gate bits: 1 outside SYSTEM, 2 fast timer unavailable, 4 PD listening, 8 CHARGER
armed. Zero permits the existing at-most-one-attempt-per-second acquisition.
ACK masks use bus-stage bit positions: bit 3 write address, bit 4 register,
bit 6 read address, bit 7 write data. A clear validity bit means unobserved,
not NACK. ID/status/calibration/control/raw validity bits are 1/2/4/8/16.
Read/STOP failures cannot publish a valid ID or raw value.

The original SYSTEM ABI remains 23 words for existing builds. The candidate adds
a matching 110-word tail in C and Rust (133 words total), with tested offsets.
USB operation 4 is added **only** for this candidate. Operations 0/1/2/3 retain
INFO/partition READ/flash diagnostics/save timing meanings. SFDP Probe's separate
operation 4 is not used by this product.

Operation 4 pages 0..10 return 10 words each. Page 0 freezes the complete RAM
record; later pages do not refresh it. Page 11 returns magic/version/word count,
sequence and CRC32 over all 440 frozen bytes. The decoder rejects corrupt/mixed
records. USB never starts an acquisition, GPIO read, SPI command, save, erase or
program operation. Software ownership remains in the ordinary main loop.

## Hardware trial

Updater follow-up, 2026-11-16: the user confirmed that the older **V1.0.2**
application rejected this image with a firmware-size error, and that **V1.0.9**
successfully wrote the same candidate. Use the T3P V1.0.9 updater. The candidate
and verified normal V5 containers are both 1,540,096 bytes. An earlier inference
from the running-app inventory that a different model's updater caused the
dialog was not established and is superseded by this user-confirmed version
comparison. Successful writing does not yet verify runtime temperature data.

1. While the completed V5 is still installed, export existing captures to a new
   directory. Firmware updates can replace that area:

   ```powershell
   cd D:\t3p_standalone_rs
   python capture_export.py --output build/captures/pre-temperature-diag-01
   ```

2. Install the separately packaged diagnostic candidate using the already tested
   T3P updater procedure. Do not substitute an ELF, raw app image or an unpacked
   file for the complete update container. The expected candidate is
   `build/temperature-diagnostics-v1/T3P_RUST_METER_TEMP_DIAG_V1_3.0.3.bin`.

3. Connect the HID port, open SYSTEM and leave it there for several seconds.
   Run the following while still on SYSTEM; the reader does not switch pages:

   ```powershell
   python temperature_read.py --output build/temperature-diagnostics-v1/device-01.json
   ```

4. Check gate=0 and timer ready. Success requires the physical ID=58, valid
   individual calibration, a successful conversion, raw ADC and a compensated
   temperature. For a failure, preserve phase/stage, ACKs and the failure-before-
   STOP GPIO sample. An address NACK alone does not prove an absent sensor.

5. A 3-second left-button hold captures any of the diagnostic screens using
   unchanged V5 save behavior. Export into another new directory:

   ```powershell
   python capture_export.py --output build/captures/temperature-diag-01
   ```

6. If needed, restore the preserved normal V5 update container at
   `build/capture-v5/T3P_RUST_METER_CAPTURE_V5_3.0.3.bin`. Its established SHA-256
   is `96b3fcd3cca9d249bc19bdb3b10ce984c38fbb2fd6623c61977e5e1fcab59620`.

The new candidate needs its own boot/USB, sensor acquisition and Capture smoke
test. Prior V5 hardware success and synthetic tests do not establish those.

## Reproduce offline

Use an isolated Cargo target and build directory; preserve all V5 artifacts.
The pinned compiler locations are listed in the handoff. Host linking needs the
configured Windows toolchain runtime.

```text
python system_source_evidence.py
python tests/check_system.py --cc PATH_TO_HOST_CLANG
python tests/check_temperature_diagnostics.py --cc PATH_TO_HOST_CLANG
cargo test --offline --features temperature-diagnostics
cargo build --offline --features temperature-diagnostics --bin preview
python preview_refs.py --temperature-diagnostics --build-dir build/temperature-diagnostics-v1 --preview PATH_TO_CANDIDATE_PREVIEW_EXE
python build.py --temperature-diagnostics --build-dir build/temperature-diagnostics-v1
python pack_demo.py --build-dir build/temperature-diagnostics-v1 --device-view build/flash-reader/device-v3-full.mapped.bin --output-name T3P_RUST_METER_TEMP_DIAG_V1_3.0.3.bin
```

Packaging runs the full linked Pi32v2 comparison, six additional diagnostic/
missing-diagnostic views, Capture save simulation, independent container unpack,
CRC validation and boot/non-app preservation. Host transport tests inject failures
at each ACK and delay stage, held lines, STOP failure, bad data, cancellation and
staleness. GPIO tests preserve unrelated PB bits and require zero PA writes.

## 日本語要点

純正のBMP ID照会はPA6/PA5ではなく、メーターと共通のPB2/PB1へ到達します。
PA側の表だけで温度バスと対応付けた点を訂正しました。修正は独立した診断候補
で試し、完成済みCapture V5配布物は保持します。PAは書き換えず初期化前後を
観測し、通信するPBは解放時・失敗時・STOP後を記録します。

候補導入前に保存画像を退避し、SYSTEMで数秒待ってから
`python temperature_read.py --output build/temperature-diagnostics-v1/device-01.json`
を実行してください。個体のID・校正値・生値・温度がまだ未確認なので、通信
失敗時や未取得時のN/Aは維持します。配布物検査・合成テストと実機成功は区別します。

## Packaged candidate validation

Offline validation completed on 2026-11-16T14:02:16.325715+09:00. Candidate: `build/temperature-diagnostics-v1/T3P_RUST_METER_TEMP_DIAG_V1_3.0.3.bin` (1,540,096 bytes).

SHA-256: `8475727d1c03dc66faf1e7bd9178aa3be0d4a5ee42d9129aa01ed87ca9f8ff14`.

SYSTEM/transport C checks: 17 passed; cached USB/decoder checks: 3 passed. Both ordinary Capture and diagnostic Rust configurations passed 41 unit and 10 Capture tests each. Capture NOR/USB/export checks, mapped-reader regression checks and Pi32v2 SRAM/USB preflight passed. The complete linked Pi32v2 run passed, including six additional diagnostic/missing-data views, Capture saving and USB pixel equality. Independent container extraction, four nested CRCs, the boot prefix and non-app preservation passed. The normal V5 update-container hash remained unchanged.

No T3P was connected during the final HID enumeration. No hardware flash was performed and no physical ID/calibration/raw/temperature acquisition is claimed. The candidate remains a hardware trial. Full private evidence: `build/temperature-diagnostics-v1/validation-complete.json` and `build/temperature-diagnostics-v1/pack-validation.log`.
