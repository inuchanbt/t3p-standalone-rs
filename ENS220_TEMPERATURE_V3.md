# ENS220 temperature V3

The subsequent normal-use candidate is [Capture V6.1](CAPTURE_V6_1.md), with
three SYSTEM pages and no comparison probes. It corrects a repaint scheduling
defect exposed by V6's failed temperature trial. The V3 hardware evidence below
continues to describe V3; its image remains preserved.

V6.1's initial hardware trial subsequently acquired 28.44 C with 21 successes
and zero failures, then saved/exported a SYSTEM BMP showing +28.38 C. The single
SAVE took 3.111 s with all V5 optimizations enabled. See the V6.1 document for its separate scope
and evidence hashes.

This separate trial implements temperature acquisition for the device that
returned ENS220 PART_ID `0321` in the [V2 hardware trial](TEMPERATURE_PROBE_V2.md#v2-on-device-result-2026-11-16).
V2 proved the ID response; the first V3 hardware trial acquired **27.52 C** with
46 successful acquisitions and one earlier failure. A second record verified
1,015 additional successful acquisitions with no additional failure over
1,015.749 seconds, reaching 27.78 C. A subsequent V3 native Capture SAVE and HID
BMP export also succeeded, with 27.64 C visible in the saved SYSTEM image.
The released Capture V5, diagnostic V1 and probe V2 binaries remain
unchanged. No sensor data from the V2 log or a photograph is compiled into the
acquisition path.

## Acquisition

The implementation is [native/t3p_ens220.c](native/t3p_ens220.c), with restricted
transport in [native/t3p_meter.c](native/t3p_meter.c). It uses the established
PB2 SCL / PB1 SDA bus, address `20` (wire `40/41`). It runs only on SYSTEM, with
the fast timer available, PD not listening and CHARGER not armed. There is no
slow timing fallback. Attempts and cleanup retries are limited to one per second.

1. Read PART_ID `00..01` and require the observed identifier `0321` before any
   configuration write. A different ID or failed read leaves N/A.
2. On the first successful initialization per boot, write `06=08` and wait at
   least 1 ms. This reset follows the stock secondary route. It is not repeated
   for each periodic measurement; an uncompleted reset transfer may be retried.
3. Write `06=80`, wait at least 1 ms, then verify the mode by reading it back.
4. Configure/read back `08=01` (one-shot), `09=00` (minimum oversampling), and
   `06=00` (idle/ultra-low power). MEAS_CFG `07` is left at its reset value;
   pressure sampling is disabled in the trigger command.
5. Wait at least 1 ms. Read and discard old `1A..1B`, then require TR=0 in
   DATA_STAT `14`. A stuck old ready flag cannot produce a published value.
6. Write `06=12` for one temperature-only measurement. Wait at least 20 ms,
   then poll DATA_STAT until TR=1, bounded by 200 ms from the trigger.
7. Read `1A..1B` in little-endian order and convert using
   `temperature_C = TEMP_OUT / 128 - 273.15`. Round to centidegrees and require
   the supported -40..85 C range before publishing.

Each reset, wake, idle-settle and conversion wait is nonblocking and bounded.
Late completion after a long capture operation is rejected rather than being
timestamped as fresh. An acquired zero degrees is valid. Errors invalidate the
display; otherwise a retained value expires after 2.5 seconds.

The single-shot operation stops itself. If configuration is cancelled or fails
while HP is enabled, a restricted idle cleanup is attempted when the normal
acquisition gate permits it. No I2C is performed while that gate is closed.
Failed cleanup retries are rate limited, and no new attempt starts until idle
has been read back. The primary failure trace is preserved separately from the
cleanup result.

The sensor measures its local die temperature. An external thermometer reading
is useful context, not a replacement for the sensor output.

## Stock comparison and permitted writes

All values below are hexadecimal. The stock secondary route was byte checked by
`system_source_evidence.py`; the new policy deliberately uses one-shot
temperature-only acquisition.

| Setting | Stock secondary route | V3 |
| --- | --- | --- |
| Address | 20 | 20, require PART_ID 0321 |
| Initial reset | 06=08 | 06=08 after verified ID, once per successful boot initialization |
| Full register access | 06=80 | 06=80 with a timed wait/readback |
| MEAS_CFG | 07=08 | Reset value retained; pressure disabled |
| STBY_CFG | 08=00 | 08=01, readback required |
| OVS_CFG | 09=22 | 09=00, readback required |
| Measurement | 06=13, P+T | 06=12, T only, one shot |

The transport write whitelist contains only `06={08,80,00,12}`, `08=01`, and
`09=00`. It cannot write calibration, UID, thresholds, interrupt settings or
arbitrary registers. The driver does not use BMP calibration or its forced-mode
command. Existing BMP builds retain their previous behavior.

Primary specification: [ScioSense ENS220 DS-29, 2025-10-27](https://www.sciosense.com/wp-content/uploads/2023/12/ENS220-Datasheet.pdf),
sections 6.3.2, 8.2.1/3/5/6/13/17 and 9.5.1. Stock delay calls pass 500; their
time unit has not been independently established here. V3 uses explicit
millisecond deadlines from the existing BSP clock.

## Diagnostic format

The SYSTEM ABI stays 133 words, the diagnostic block 110 words, and Capture
operations 0..3 retain their behavior. Operation 4 still freezes a cached block
on page 0, with pages 1..10 and a CRC trailer at page 11. USB access never starts
sensor communication. Operation 5 retains the eight V2 probe records.

V3 uses the same diagnostic magic with **version 3 and word 39 sensor_kind=2**:

| Field | ENS220 interpretation |
| --- | --- |
| ID | 16-bit PART_ID, displayed as 0321 |
| status | DATA_STAT 14 |
| adc / word 21 | Raw TEMP_OUT counts, 1/128 K; exported as raw_temperature_counts |
| cal0 / word 22 | Readback mask: bit0 MODE80, bit1 STBY01, bit2 OVS00, bit3 MODE00 |
| cal1 / word 23 | Low byte: 0 not needed, 1 deferred/required, 2 idle verified, 3 I/O failure, 4 mode mismatch; following bytes: cleanup bus error, stage, STOP error |
| control | Trigger 12, valid only after its successful write |
| calibration | Unacquired/unused; never treated as BMP coefficients |

The host decoder accepts V1/V2 unchanged. It checks V3 sensor/validity/range
fields, preserves null for unacquired values and rejects unknown versions.
The UI label is **ENS220 TEMP**, APP **T3P ENS220 TEMP V3**, HID product
**T3P ENS220 Temp V3**. Cached failure/PB/PA diagnostic sections remain available.

## Hardware trial

Use the working **T3P PC updater V1.0.9**. Export any wanted captures before
changing firmware. Candidate path:

```text
D:\t3p_standalone_rs\build\temperature-ens220-v3\T3P_RUST_METER_ENS220_TEMP_V3_3.0.3.bin
```

After flashing, open SYSTEM and leave it visible for at least ten seconds:

```powershell
cd D:\t3p_standalone_rs
python temperature_read.py --output build/temperature-ens220-v3/device-ens220-01.json --summary
```

The file contains complete traces and CRC-protected snapshots; `--summary`
only shortens console output. Expected evidence is sensor ENS220, ID 0321,
increasing successes, a valid temperature, readback mask 0F, trigger 12 and
raw-temperature counts. A failed trial must retain its exact stage/result.
Shared I2C read-error counters also include the V2 matrix's nonresponding BMP
and PA targets (five address NACKs in the observed V2 pass). Use the dedicated
`temperature.failures` counter to judge ENS220 acquisition.
Record a second snapshot after about 30 seconds with a new filename:

```powershell
python temperature_read.py --output build/temperature-ens220-v3/device-ens220-02.json --summary
```

Compare updated_ms/success counters and the actual readings. Leave SYSTEM and
return to check cancellation/recovery. Use the established Capture gesture on
SYSTEM and export that new screenshot to verify V5 Capture on this candidate;
initialization status alone is not a save/retention test.

The agent's runtime did not enumerate the physical HID device during preparation;
the user's PC must perform the flash/button operations and save these records.
No hardware temperature result has been inferred from the synthetic previews.

## Reproduce offline

```text
python tests/check_ens220.py --cc PATH_TO_HOST_CLANG
python tests/check_system.py --cc PATH_TO_HOST_CLANG
python tests/check_temperature_diagnostics.py --cc PATH_TO_HOST_CLANG
python tests/check_temperature_probe.py --cc PATH_TO_HOST_CLANG
cargo test --offline --features temperature-ens220
cargo build --offline --features temperature-ens220 --bin preview
python preview_refs.py --temperature-ens220 --build-dir build/temperature-ens220-v3 --preview PATH_TO_PREVIEW
python build.py --temperature-ens220 --build-dir build/temperature-ens220-v3
python tests/check_capture_device.py --cc PATH_TO_HOST_CLANG --build-dir build/temperature-ens220-v3 --device-view build/flash-reader/device-v3-full.mapped.bin --device-sfdp build/capture-sfdp-probe/device-sfdp-20261115-231927.bin
Copy-Item build/temperature-probe-v2/probe-usb-check.frames build/temperature-ens220-v3/probe-usb-check.frames
python tests/check_capture_probe_pi32.py --build-dir build/temperature-ens220-v3
python pack_demo.py --build-dir build/temperature-ens220-v3 --device-view build/flash-reader/device-v3-full.mapped.bin --output-name T3P_RUST_METER_ENS220_TEMP_V3_3.0.3.bin
```

The state checks cover ID refusal, every transfer failure, configuration
mismatch, stale-ready rejection, cancellation at every pending stage, deferred
cleanup/retry, long waits, timer wrap, valid zero and range handling. The
transport checks exercise real C I2C framing/address/read order and refusal of
unlisted writes. Pi32 checks execute conversion edge cases and frozen V3 USB
snapshots with hardware MMIO forbidden. They do not establish real sensor timing.
Capture qualification uses the previously acquired private mapped-device/SFDP
inputs. The host probe check writes its C wire fixture into the V2 directory;
the copy above supplies that unchanged fixture to the V3 target-cache test.

The pre-change V2 source is preserved in
`build/temperature-probe-v2/source-before-ens220-v3.zip`. Source checkpoints and
logs describe their respective build times; later device-trial notes do not
change an existing image or retroactively mark its temperature hardware verified.

## Completed offline validation, 2026-11-16

Candidate: `T3P_RUST_METER_ENS220_TEMP_V3_3.0.3.bin`; **1,540,096 bytes**.
SHA-256: `39bd754162ef924539e76077ff46bcfe76fd69495e56526ebdc64a87b2a883e2`.
ELF SHA-256: `f01f00af674164eecd81af62e8a4a2994fde94cde685f540c9ec39b5ba4702d7`.
Static RAM: 27,920 bytes, with the existing driver/stack reservations retained.

Passed: 19 SYSTEM C cases; two ENS220 C suites plus five Python checks;
41 Rust unit and ten capture checks; V1 cache compatibility; V2 probe/GPIO
checks; the full Capture NOR/USB regression suite. The linked Pi32 preflight
executed ten ENS220 conversion cases, twelve frozen V3 temperature USB pages,
98 V2 probe pages, eight profile/USB cases and four SRAM-entry cases. Cached
USB execution reached no GPIO/SPI MMIO.

The final packaging run passed native SAVE/USB decoding against all 57,600
frozen RGB565 pixels, all PD/CABLE/CHARGER/SYSTEM comparisons and long graphs.
Independent unpacking matched full plaintext, boot content and 44 non-application
files, with four nested CRCs verified. V5/V1/V2 binaries retained their original
hashes. These are offline checks, not a V3 physical temperature result.

Evidence: `build/temperature-ens220-v3/pack-validation.log`, the image's
`-audit.json`, `OFFLINE-probe-pi32.json`, `validation-complete.json` and
`source-checkpoint.zip`. Hardware execution was pending at packaging time; the
subsequent physical observation is recorded below.

## First V3 hardware temperature result, 2026-11-16

The user saved `build/temperature-ens220-v3/device-ens220-01.json`:
SHA-256 `484f3e3d3a7c838479b3b3dcfc8ffbcc902691a5fe594578dc615637cdb8c8f0`.
The complete main diagnostic and eight-record probe caches were reconstructed,
CRC checked and decoded again, exactly matching the saved JSON. Temperature
CRC32 is `A62E82FC`; the 3,880-byte probe cache CRC32 is `AE04D7B1`.

| Item | Physical observation |
| --- | --- |
| Device / sensor | T3P ENS220 Temp V3 / ENS220 |
| Bus / ID | PB2/PB1, 7-bit 20 / PART_ID 0321 |
| Temperature | 27.52 C |
| TEMP_OUT counts | 38486; 38486 / 128 - 273.15 = 27.521875 C |
| Attempts / successes / failures | 47 / 46 / 1 |
| Last operation | Read 1A..1B, DONE / OK |
| DATA_STAT / trigger | 01 / 12 |
| Configuration readback mask | 0F |
| ACKs / STOP | Write address, register and read address ACKed; no STOP error |
| Last attempt / update | 57787 / 57887 ms; elapsed 100 ms |
| Gate / fast timer | Gate 0; ready |
| Probe completion / PA restore | 8/8, not halted; all three PA restores matched |

**Initial real temperature acquisition is verified.** The conversion uses the
acquired register data, without a fixed display value or BMP calibration.
The one earlier failure cannot be assigned to a stage from this latest-success
cache: V3 does not retain per-attempt failure history. It is the ENS220 task's
failure counter, separate from the matrix's expected nonresponding targets.
The earlier failure's phase and cause remain unresolved without its own trace.

Capture initialization status/step/error are all zero and the three V5
optimization flags are enabled. SAVE generation is zero, so this record does
not verify a new V3 Capture SAVE or power-cycle retention test. Next compare
the second snapshot's success/failure counters **before** taking a new SYSTEM
Capture on V3. A long SAVE can expire a pending 200 ms conversion; that case
must be distinguished from a transport failure when examining later counters.
The observation audit is
`build/temperature-ens220-v3/hardware-validation-01.json`; original JSON and
firmware files are preserved. No code or image was changed for this analysis.

## Continued V3 hardware result, 2026-11-16

The second saved record is `build/temperature-ens220-v3/device-ens220-02.json`:
SHA-256 `5de443d6259468d9ce0297fbcb48067bedbc6ced0a7ee781c53c41edfa7a1d24`.
Both full main/probe caches were CRC checked and decoded exactly against the
saved fields. The second main CRC32 is `B2C55ED8`; probe CRC32 remains `AE04D7B1`.

| Item | Record 01 | Record 02 | Difference |
| --- | ---: | ---: | ---: |
| Attempts | 47 | 1062 | +1015 |
| Successes | 46 | 1061 | +1015 |
| Failures | 1 | 1 | 0 |
| Temperature C | 27.52 | 27.78 | +0.26 |
| TEMP_OUT counts | 38486 | 38519 | +33 |
| Updated ms | 57887 | 1073636 | +1015749 |
| ENS220 transactions | 646 | 14856 | +14210 |

**All 1,015 additional counted attempts succeeded across this approximately
17-minute observation interval.** The transaction increase is exactly 14 per
additional successful attempt, matching the post-reset acquisition path.
Record 02 still has ID 0321, configuration mask 0F, DATA_STAT 01, trigger 12,
DONE/OK, all three read-transaction ACKs and no bus/STOP error. Its raw value
converts to 27.7796875 C, matching the rounded 27.78 C. Its last attempt took
95 ms (1073541 to 1073636 ms). The eight one-shot probe records are unchanged.

The existing failure occurred before record 01; its cause remains unknown.
This interval establishes continued acquisition, not unlimited uptime,
thermometer accuracy or behavior through every power/PD transition.
Capture initialization in record 02 remains ready, with all V5 flags enabled
and SAVE generation still zero. The subsequent Capture trial below verifies
SAVE and USB export. Interval evidence:
`build/temperature-ens220-v3/hardware-validation-02.json`. No firmware changes
or extra temperature trial are required by this successful interval alone.

## V3 hardware Capture result, 2026-11-16

After the temperature interval check, the user captured SYSTEM and ran:

```powershell
python capture_export.py --output build/temperature-ens220-v3/capture-01
```

The exported `capture-01/SCRN0001.BMP` was read locally, SHA checked against its
sidecar, decoded and visually inspected. Its PNG preview was pixel compared
with the BMP. The exporter's two independent acquisitions of the flash header
and compressed data matched before publishing the image.

| Item | Physical observation |
| --- | --- |
| Saved image | SCRN0001.BMP, 240 x 240, 115266 bytes |
| Stored compressed bytes | 13960, slot 0 |
| BMP SHA-256 | df959721021bccb860bc31513511d3729c883dae78dc09f63a0730baa2121690 |
| Native SAVE | Generation 1, result 1, completed, timer valid |
| Total time | 3110 ms; one trial with this image |
| Phase times | Notice 796, prepare 1569, journal 5, render 33, encode 475, finish 232 ms |
| V5 optimization flags | All three enabled; verified blank claim consumed once |
| Backend | Status/initialization step/I/O error all 0 |
| Visible SYSTEM state | APP T3P ENS220 TEMP V3; BASE IMAGE 3.0.3 |
| Visible sensor/meter | ENS220 TEMP +27.64 C; meter 2260, config 4127, state OK |

**The ENS220 temperature correction and V5 Capture SAVE/HID-export integration
are verified on this V3 candidate for the recorded scope.** The saved image
contains the actual SYSTEM temperature. It is not the synthetic host preview.
The 3110 ms measurement is not a controlled speed comparison with older V5
images. This trial does not add a new V3 power-cycle retention result; the
released V5's earlier retention evidence remains separate and unchanged.

Aggregate evidence is
`build/temperature-ens220-v3/hardware-validation-complete.json`, with the
original BMP, export sidecar and device JSONs preserved. The candidate image
hash remains `39bd754162ef924539e76077ff46bcfe76fd69495e56526ebdc64a87b2a883e2`.
No firmware was changed during result verification.

## 日本語要点

実機V2で取得したENS220のID 0321を条件に、V3で温度専用の単発変換を追加します。
初回だけ純正と同じリセット・起動を行い、古い完了フラグの消去、設定読み戻し、
新しい測定完了、生データ、範囲を確認してから表示します。失敗・古い値はN/Aです。
SYSTEM表示・fast timer・PD/CHARGERの条件を維持し、CAPTURE V5の保存処理を変更
しません。書き込み後は上の01/02 JSONと、SYSTEMのCAPTUREで実機確認します。
オフラインの合成値と温度の実取得は区別します。

実機01ログで温度27.52℃を取得しました。生データ38486の換算、設定0F、ID0321、
通信ACKと診断CRCが一致し、47試行中46成功・1失敗です。過去1回の失敗工程は履歴が
残らないため未確定です。次は02ログで成功回数の増加と失敗回数の推移を確認し、
V3でSYSTEMのCAPTURE保存・取り出しを確認します。

02ログでは約16分56秒の間に1,015回成功が増え、失敗は1のままでした。温度は27.78℃、
設定・ACK・CRC・換算も一致しています。この区間の継続取得は確認済みです。
02時点でCAPTURE保存回数は0のため、次はV3でのSYSTEM保存・USB取り出しを確認します。

その後、V3でSYSTEMを保存・USB取り出しできました。BMPは115,266バイト、独立2回の
読み取りが一致し、SHA-256と画素も確認済みです。実画像にはENS220 TEMP +27.64℃、
METER STATE OKが表示されています。保存は今回1回で3.110秒、V5最適化3項目も有効です。
今回の範囲で、ENS220温度修正とCAPTURE V5の保存・取り出し維持を実機確認しました。
