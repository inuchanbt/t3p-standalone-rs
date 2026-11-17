# Capture V6.1: service ENS220 during LCD repaint

Capture V6.1 fixes a scheduling defect exposed by the V6 hardware trial:
temperature acquisition could remain N/A even though the ENS220 ID and
configuration-start writes succeeded. Temperature was polled only outside the
full LCD repaint. A repaint longer than the 200 ms pending-state deadline could
expire reset/wake waits. With acquisition and repaint both scheduled every
second, subsequent attempts could encounter the same delay repeatedly.

The initial hardware trial on 2026-11-16 acquired 28.44 C with 21 completed
acquisitions and zero failures. SYSTEM Capture SAVE and HID BMP export also
succeeded; the actual image shows ENS220 TEMP +28.38 C. This confirms the initial
V6.1 temperature/Capture regression trial described below.

## Hardware evidence and reproduction

The preserved [V6 trial](CAPTURE_V6.md) recorded product T3P Capture V6, ENS220
ID 0321, 16 attempts, zero successes, 15 failures and 60 transactions. The frozen
snapshot was in a new configuration attempt, with mask 00, idle cleanup deferred,
and no temperature. Its result OK and bus error none describe that active
attempt; they do not mean a temperature was acquired or describe earlier failed
attempts. V3's diagnostic ABI does not retain their individual failure records.

`tests/board_temperature_schedule.c` executes the actual board C loop and ENS220
state machine against synthetic sensor/time/LCD boundaries. A 26 ms strip gives
a 780 ms full repaint. With the archived V6 board source, it reproduces the
snapshot's 16/0/15/60 counts and active configuration state exactly. With the
corrected board source, the same case completes 15 acquisitions, has zero
failures, and has one attempt in progress at the snapshot.

| At the sixteenth attempt | Archived V6 | Corrected V6.1 |
| --- | --- | --- |
| Attempts | 16 | 16 |
| Completed acquisitions | 0 | 15 |
| Failures | 15 | 0 |
| Attempt still in progress | 1 | 1 |
| Sensor transactions | 60 | 213 |

The synthetic strip duration is a reproduction boundary, not an independently
measured electrical or V6 frame-time trace. The code defect and its matching
failure pattern are reproduced. The new firmware's initial hardware confirmation
is recorded below. V3's earlier single failure remains historically unresolved.

V6's Capture operation succeeded independently: the actual 240 x 240 exported
BMP is 115,266 bytes, stores 13,746 compressed bytes and shows ENS220 TEMP N/A.
Its SHA-256 is `65dd389bfdc1f100cc06e233b2f50312e1488557c6307a573cc0f70786dfe85b`.
The export reports two matching acquisitions; local hash, BMP decoding and PNG
pixels agree. SAVE result/generation were 1/1, the backend was ready without an
I/O error, and all three V5 optimizations were enabled. Its single SAVE took
3.098 s. This is not a controlled speed comparison or a new power-cycle trial.

Local records are `build/capture-v6/hardware-validation-01.json`,
`build/capture-v6/device-v6-01.json`, `build/capture-v6/capture-01/`, and
`build/capture-v6-1/OFFLINE-temperature-schedule.json`.

## Change and retained behavior

The normal repaint loop now services ENS220 after each eight-row strip, after
key and PD polling. Each call uses a fresh BSP timestamp and rechecks SYSTEM,
fast-timer availability, PD listening and armed CHARGER gates. The host board
regression also changes these gates inside a repaint and checks that no sensor
transaction occurs while they are closed. Timer-unavailable operation performs
zero sensor transactions.

The Rust display snapshot remains frozen throughout a frame. The Capture SAVE
and notice renderers use their existing paths and gain no sensor calls. A
pending measurement delayed beyond its deadline by a multi-second SAVE still
expires and recovers under the existing policy.

ENS220 ID verification, command whitelist, state machine, configuration
readback, conversion formula, stale-ready rejection, 200 ms deadline, freshness
and cleanup policy are unchanged. The Capture storage and profiling sources
are unchanged. V6.1 retains three SYSTEM pages, excludes the eight-target
comparison probe and retains cached temperature USB operation 4. Comparison
operation 5 remains refused. The old V5, V1, V2, verified V3 and failed V6 images
and their source/evidence checkpoints remain preserved.

The build option remains `--capture-ens220`; its default output directory is
now `build/capture-v6-1`. The normal HID product is T3P Capture V6.1 and SYSTEM
APP is T3P RS CAPTURE V6.1. Host readers also continue to recognize older names.

## Hardware result (2026-11-16)

`build/capture-v6-1/device-v6-1-01.json` records HID product T3P Capture V6.1,
ENS220 ID 0321, 21 attempts, 21 successes, zero failures and 295 transactions.
The completed sample is 28.44 C from 38,603 counts (28.4359375 C before
centidegree rounding). Status is 01, the trigger value written is 12, configuration mask
is 0F, cleanup is idle verified, and bus/STOP errors are none. The cached start
and completion timestamps differ by 90 ms; that is acquisition latency, not a
logic-analyzer measurement of wire time. No comparison-probe object is present.

The full raw-word snapshot was reconstructed and decoded again; CRC32
6D479ABC and every decoded field match the saved JSON. Its SHA-256 is
`cb5eb3d78828ad73d6d7d3006eae47e831bd34d8e6a9152a61eb5611c7dd7f74`.

The subsequent native SAVE/HID export produced a valid 240 x 240 RGB BMP,
115,266 bytes, with 14,006 compressed bytes in slot 0. Its SHA-256 is
`42904166ed8176a8ffa625011d0bf6ed5e92506657a27024efa381626d544081`.
The exporter reports two matching acquisitions. The local hash, BMP decoding
and PNG pixels were checked. The actual image shows APP T3P RS CAPTURE V6.1,
SYSTEM 1/3, ENS220 TEMP +28.38 C, meter 2260/config 4127/state OK, uptime 00:00:50.
The JSON and image are separate observations at different times.

SAVE generation/result are 1/1, backend status/step/I/O error are zero, and all
three V5 optimization flags are enabled. This single SAVE took 3,111 ms. It is
not a controlled speed comparison with another captured image.

The verified scope is these 21 acquisitions and one subsequent SYSTEM SAVE/HID
export. This record adds no V6.1 long-duration, post-SAVE temperature-recovery,
or power-cycle retention claim. V3's earlier long interval and V5's earlier
retention evidence remain attached to those original images.

The hardware audit is `build/capture-v6-1/hardware-validation-01.json`; the BMP,
sidecar and lossless PNG are in `build/capture-v6-1/capture-01/`. Build-time
audits and `source-checkpoint.zip` retain their original offline/pending status
and source hashes; this subsequent hardware result is recorded separately.

## Recorded hardware procedure

Use the working T3P PC updater V1.0.9 and this new container:

```text
D:\t3p_standalone_rs\build\capture-v6-1\T3P_RUST_METER_CAPTURE_V6_1_3.0.3.bin
```

Confirm APP T3P RS CAPTURE V6.1, keep SYSTEM visible for about 15 seconds and run:

```powershell
cd D:\t3p_standalone_rs
python temperature_read.py --output build/capture-v6-1/device-v6-1-01.json --summary
```

Check acquired temperature and increasing successes. For a completed attempt,
phase is done, configuration mask is 0F and cleanup is idle verified. A snapshot
during the next attempt may show configure or wait; assess its accumulated
successes and temperature validity together. The format remains diagnostic V3
despite the firmware's V6.1 name. No probes object is expected.

Then save a SYSTEM Capture using the established gesture and export:

```powershell
python capture_export.py --output build/capture-v6-1/capture-01
```

Inspect the actual BMP, export sidecar and backend/timing metadata. This runtime
has not enumerated the physical HID device; flash and button operations remain
on the user's PC.

## Reproduce offline

```text
python tests/check_temperature_schedule.py --cc PATH_TO_HOST_CLANG --build-dir build/capture-v6-1 --baseline-checkpoint build/capture-v6/source-checkpoint.zip
python tests/check_system.py --cc PATH_TO_HOST_CLANG
cargo test --offline --features capture-ens220
cargo build --offline --features capture-ens220 --bin preview
python preview_refs.py --capture-ens220 --build-dir build/capture-v6-1 --preview PATH_TO_PREVIEW
python build.py --capture-ens220 --build-dir build/capture-v6-1
python tests/check_capture_device.py --cc PATH_TO_HOST_CLANG --build-dir build/capture-v6-1 --device-view build/flash-reader/device-v3-full.mapped.bin --device-sfdp build/capture-sfdp-probe/device-sfdp-20261115-231927.bin
python tests/check_capture_probe_pi32.py --build-dir build/capture-v6-1 --capture-ens220
python pack_demo.py --build-dir build/capture-v6-1 --device-view build/flash-reader/device-v3-full.mapped.bin --output-name T3P_RUST_METER_CAPTURE_V6_1_3.0.3.bin
```

The baseline comparison requires the preserved V6 source checkpoint. It extracts
only the board source into the new build directory; it does not replace current
source files. Reports identify the synthetic boundaries and source hashes.
Full packaging checks compiled Pi32 frames/actions, native Capture pixels and
independent unpacking/checksums. Hardware results remain separate from these
offline checks.

## Completed offline validation

The new V6.1 container passed full offline packaging validation on 2026-11-16.
These build-time offline results preceded the successful hardware trial above.

| Artifact or check | Result |
| --- | --- |
| Update container | 1,540,096 bytes |
| Container SHA-256 | `9cdb79dd01c6860c2808b18122e30ca3ca6f09bf51f5120a8622b791501cbb51` |
| Linked ELF SHA-256 | `13bb144a6514b3f4036a550e6aba8c0acd1205047b05b95508adedd42cd4ca5d` |
| Audited static RAM | 20,152 bytes |
| Rust tests | 41 unit + 10 Capture |
| SYSTEM C checks | 21 passed, including five scheduling/gate scenarios |
| Host references | 63 frames generated |
| Native Capture | SAVE/HID decode matches all 57,600 RGB565 pixels |
| Pi32 frames/actions | PD, CABLE, CHARGER, 3-page SYSTEM and graphs passed |
| Cached ENS220 preflight | 10 conversion cases and 12 pages; no GPIO/SPI MMIO |
| Comparison code | No probe symbols; operation 5 refused without MMIO |
| Independent unpacking | Full plaintext match; 44 other files and boot files match |
| Nested checksums | All 4 checked |
| Board scheduling regression | Archived V6 starvation reproduced; five corrected/gate scenarios passed |

The build directory contains `validation-complete.json`, package/build audits,
the full validation log and `source-checkpoint.zip`. V5, V1, V2, V3 and V6 container
hashes were also checked against their preserved values. These are offline
results; V6.1's subsequent physical evidence is identified separately above.


## 日本語要点

V6ではENS220のIDは読める一方、温度が0成功・15失敗でした。全画面描画中に温度処理を
呼ばず、200msの締切を超える不具合を、実際の基板ループを使う模擬試験で再現しました。
修正前は実機と同じ16試行・0成功・15失敗・60通信、修正後は15成功・0失敗・1処理中です。
V6.1は8行描画ごとに温度処理を進め、毎回SYSTEM・PD・CHARGER・タイマー条件を確認します。
温度設定・換算式・締切・CAPTURE保存処理は維持します。
V6.1の初回実機確認では28.44℃・21成功・失敗0、設定読戻し0F・待機復帰正常でした。
CAPTURE保存とHID取り出しも成功し、実画像にV6.1と+28.38℃を確認しました。
今回1回の保存は3.111秒、V5最適化も有効です。この結果の範囲で初回の退行確認を完了します。
