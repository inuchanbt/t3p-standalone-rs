# Temperature probe V2

The subsequent implementation is [ENS220 temperature V3](ENS220_TEMPERATURE_V3.md).
V2's ID observation below is hardware evidence; the subsequent first V3
temperature result is recorded in its own document.

V1 narrowed the physical failure to the BMP ID phase. V2 adds a **finite,
register-read-only matrix**, preserving V5 Capture, the V1 diagnostic ABI and
the V1 temperature-conversion policy. No temperature success is claimed yet.

## V1 physical observation, 2026-11-16

The user installed `T3P Temp Diag V1` using the T3P PC application V1.0.9 and
saved `build/temperature-diagnostics-v1/device-after-flash-20261116-01.json`.
The saved record was read locally and its 440-byte diagnostic CRC was checked:

- JSON SHA-256: `729bc4d9e1add81823afad9c78f841745030a087a37f2632c1487a83eaf873ac`.
- Diagnostic CRC32: `8D5DDA86`; sequence 34, attempts 17, failures 17, successes 0.
- Temperature-only transactions 17, one per attempt. No attempt progressed to
  the status/calibration/control transactions needed after ID=58.
- The **last cached transaction** failed at the write-address ACK for wire
  `EE` (7-bit `77`), before transmitting register D0. ID/status/calibration/raw/
  control/temperature are unacquired in that record.
- Gate mask 0, fast timer ready, timer CON `00001801`, STOP error none.
- PB input/direction masks `06`; both lines read high at release, address NACK
  and after STOP. PB DIE `06`, DIEH `00`, PU0 `06`, PU1/PD/drive masks `00`.
- TIMER1_COUNT advanced from `6509` at release to `791F` at failure and `7C5A`
  after STOP. This observation does not show a stopped timer or a held-low line.
- PA5/PA6 configuration was unchanged; input/direction/DIE/DIEH masks `60`,
  pull-up/down and drive masks `00`.
- Capture backend status/initialization step/error all 0, JEDEC `856015`, SFDP
  `50444653`, all three V5 optimization flags enabled. There was no completed
  SAVE after boot; this is initialization evidence, not a new save/retention test.

An earlier description that all 17 attempts specifically NACKed was too strong:
V1 retains the last transport failure, not a per-attempt history. The counters
bound all attempts to the ID phase, while the specific address NACK is directly
observed only for the last one. PB-only rerouting did not produce a temperature.

The byte-verified stock call route remains PB2/PB1; this identifies what the
stock code requests, not which chip is physically fitted to this board. A
power/configuration dependency, alternate address, variant wiring, absent or
different device remains unresolved. No reset or conversion-setting change can
be justified as a repair for an unacknowledged device address alone.

Stock also has a secondary route when its sensor selector remains zero at
`0C0E6E14`: `0C0E6E1E -> 0C00EBD6 -> 0C0CF57A` writes, and
`0C0E6E6E -> 0C00EBF4 -> 0C0CF502` reads. Both small helpers pass wire address
`40` (7-bit `20`) to the same software-I2C wrappers. Four call instructions and
four immediate instructions were checked against the stock app bytes; the
record is `build/temperature-probe-v2/stock-secondary-route.json`.

**ENS220 is now a supported hypothesis for this stock route:** the manufacturer's
data sheet specifies address 20, reset/high-power sequence `06=08`, wait 0.5 ms,
`06=80`, wait 0.5 ms, and UID at 02..05. These match the stock sequence and its
four-byte read. Stock delay calls pass 500; their time unit has not been verified
here. This identifies a software fingerprint, not this unit's fitted
sensor. V2 reads only PART_ID at 00..01 for this target, also accessible in low
power mode; it does not reproduce the reset or measurement writes. The raw bytes
are retained in wire order and `part_ID_little_endian` interprets this register's
byte order. No constant ID is accepted as a hardware result. Source:
[ScioSense ENS220 data sheet, sections 7.2 and 8.2.1..8.2.3](https://www.sciosense.com/wp-content/uploads/2025/10/ENS220-Datasheet-1.pdf).

The first offline packaging run was interrupted after the cable checks when
this evidence was found. Its ELF/log are preserved as `pre-ENS220.elf` and
`pack-validation-pre-ENS220.log`. The final candidate replaces the planned touch
status read with ENS220 PART_ID, keeping eight reads and the same cache ABI.

## Finite probe matrix

| Index | Pins | 7-bit address | Register | Bytes | Purpose |
| --- | --- | --- | --- | --- | --- |
| 0 | PB2/PB1 | 40 | FF | 2 | Meter die ID; known transport control |
| 1 | PB2/PB1 | 22 | 01 | 1 | PD controller device ID; second control |
| 2 | PB2/PB1 | 76 | D0 | 1 | BMP alternative address |
| 3 | PB2/PB1 | 77 | D0 | 1 | BMP stock address |
| 4 | PA6/PA5 | 19 | 0F | 1 | Stock accelerometer ID |
| 5 | PB2/PB1 | 20 | 00 | 2 | ENS220 PART_ID; stock secondary route |
| 6 | PA6/PA5 | 76 | D0 | 1 | BMP alternative address on PA |
| 7 | PA6/PA5 | 77 | D0 | 1 | BMP stock address on PA |

The BMP280 data sheet (revision 1.26, section 5.2, p.28) specifies both 76 and
77 according to SDO. CSB must select I2C, and the data sheet describes loss of
I2C response if startup selected SPI (section 5.1). These are hypotheses to
distinguish, not a finding about this unit's SDO, CSB or supply wiring. Source:
[Bosch BMP280 data sheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf).

This is not a full address scan. Each target performs one existing register-read
transaction, at most one per second, once per boot. It runs only while SYSTEM is
visible, the fast timer is ready, PD is not listening, and CHARGER is not armed.
Leaving SYSTEM pauses the remaining probes; it does not restart completed ones.
No target reset, power/CSB/mux change or new measurement-control write is added.
The original gated temperature acquisition continues separately and may still
write its existing F4=21 **only after** verified BMP ID/calibration, as in V1.

PB reuses the meter's existing GPIO setup. For each PA read, PA5/PA6 are released
first, output latches set low, digital input enabled, DIEH cleared, 10k internal
pull-ups enabled, other pulls/drives cleared. Only those two bits are changed.
The saved configuration is restored after success or failure, with direction
restored last, and the changed bits are read back. Restoration mismatch halts
the remaining matrix. SPL/CON and peripheral routing are observed, not changed.
Register-pointer transmission is part of a read; no sensor configuration data
is written by the matrix.

Each target retains its value-valid flag, transport stage/error/ACKs and GPIO
before initialization, after initialization, released, failure-before-STOP and
after STOP. The PA restore flag describes the subsequent restoration check;
the GPIO snapshots themselves precede that restoration. The probe's generic
`ID` field contains the acquired register value (e.g. 2260 for the meter), not
an assertion that every target is a BMP sensor. Unacquired values remain null.
Use each record's `started_ms`/`finished_ms` for probe timing; the nested V1
acquisition scheduler timestamps are not populated by these one-shot reads.

## Trial

Use the **T3P PC updater V1.0.9**. Export any wanted captures before changing
firmware, using a fresh output directory. Keep the V1 JSON as the baseline.

Candidate location:

```text
D:\t3p_standalone_rs\build\temperature-probe-v2\T3P_RUST_METER_TEMP_PROBE_V2_3.0.3.bin
```

After installing, the HID product is `T3P Temp Probe V2` and the SYSTEM APP label
is `T3P TEMP PROBE V2`. SYSTEM retains the six existing sections; matrix results
are collected in RAM and exported in JSON, without adding a new button gesture.

Open SYSTEM and leave it visible for **at least 10 seconds**, then run:

```powershell
cd D:\t3p_standalone_rs
python temperature_read.py --output build/temperature-probe-v2/device-probe-01.json
```

The new `probes` object should have `completed: 8`, `halted: false`. If it is
incomplete, keep SYSTEM open and save another record to a different filename.
USB reads do not initiate or repeat probes. A new probe pass requires a reboot.

Interpretation:

- Meter=2260 and an acquired PD ID establish positive controls on PB in this pass.
- BMP ID=58 at 76 identifies an address difference; at PA identifies a different
  responding route. This still precedes calibration/conversion verification.
- An acquired different ID is preserved for inspection; do not call it BMP280.
- An acquired ENS220 PART_ID is evidence for the secondary device route. Inspect
  its actual bytes/revision before implementing a temperature-conversion path.
- Positive PB controls with no BMP response constrain the failure to the target
  address/device/availability; they do not prove absence.
- A PA accelerometer response establishes a control for that GPIO pair under
  the tested input/pull configuration. Failure of every PA target remains
  ambiguous between topology, availability, pin configuration and signaling.
- Do not replace N/A with a fixed temperature or calibration.

## Cache protocol and checks

Existing Capture operations 0..4 are unchanged. V2 alone adds operation **5**:
pages 0..96 return a frozen 970-word RAM snapshot; page 97 returns magic/version,
word count, generation and CRC32 over all 3,880 bytes. Page 0 freezes the cache;
later pages never refresh it. No GPIO, I2C, SPI, save, erase or program is triggered
by a USB request. The host rejects corrupt/mixed snapshots and unlisted targets.

Header words: magic TPR2, version 2, words 970, record words 120, targets 8,
completed count, generation, acquisition enabled, fast-timer readiness, halted.
Each record has ten metadata words followed by the unchanged 110-word V1 trace.
Metadata: port, address, register, length, signed transport result, value-valid,
big-endian observed value, start ms, finish ms, PA restore (0 N/A, 1 matched,
2 mismatch). The SYSTEM C/Rust snapshot ABI remains 133 words for both candidates.

```text
python tests/check_temperature_probe.py --cc PATH_TO_HOST_CLANG
python tests/check_temperature_diagnostics.py --cc PATH_TO_HOST_CLANG
python tests/check_system.py --cc PATH_TO_HOST_CLANG
cargo test --offline --features temperature-probe
cargo build --offline --features temperature-probe --bin preview
python preview_refs.py --temperature-probe --build-dir build/temperature-probe-v2 --preview PATH_TO_PROBE_PREVIEW
python build.py --temperature-probe --build-dir build/temperature-probe-v2
python tests/check_capture_probe_pi32.py --build-dir build/temperature-probe-v2
python pack_demo.py --build-dir build/temperature-probe-v2 --device-view build/flash-reader/device-v3-full.mapped.bin --output-name T3P_RUST_METER_TEMP_PROBE_V2_3.0.3.bin
```

The completed V5 and V1 update containers are preserved. The V1 source checkpoint
is `build/temperature-diagnostics-v1/source-before-probe-v2.zip`. Offline fake
register inputs and injected ID values never constitute hardware sensor results.

## Completed offline validation, 2026-11-16

Final candidate: `T3P_RUST_METER_TEMP_PROBE_V2_3.0.3.bin`; **1,540,096 bytes**.
SHA-256: `adff30e325b65cc5381f2322d95cef8e4ec6bcb38cea563b1134d2a1d6c5cd54`.
ELF SHA-256: `35757d5ab467cb3cd8a1e705be9605381ee71263f930b5de83ca748d0100b337`.
Static RAM is 27,920 bytes; the existing driver and stack reservations still fit.

The final ENS220-inclusive build passed the full linked Pi32 display and Capture
suite, including pixel comparison, native SAVE/USB decode, PD/CABLE/CHARGER,
SYSTEM diagnostic sections and long-history graphs. Independent unpacking
matched the complete plaintext, all 44 non-application files and boot content;
four nested CRCs were checked. Native C/host checks cover finite scheduling,
GPIO restoration, corruption rejection and V1 cache compatibility. Target cache
replay checked 98 USB pages, a frozen CRC and zero GPIO/SPI MMIO accesses.
Rust probe-feature tests passed 41 unit and ten capture checks.

Evidence: `build/temperature-probe-v2/pack-validation.log`, the image's
`-audit.json`, `OFFLINE-probe-pi32.json`, `validation-complete.json` and
`source-checkpoint.zip`. The original V5/V1 image hashes were rechecked.
These describe validation at packaging time. The subsequent on-device ID
trial is recorded below; temperature acquisition remains unverified.

## V2 on-device result, 2026-11-16

The user installed V2 and saved
`build/temperature-probe-v2/device-probe-01.json`. The file was read locally:
SHA-256 `1125eecc6bc8b6413796218c8cbd1978bf0f87b89b33684e241b720d98bf7add`.
All nested diagnostic CRCs were checked against their raw words. Reconstructing
the complete 3,880-byte probe cache, including metadata and header, reproduced
CRC32 `1E22B4CE`. All eight targets completed, generation 9, timer ready, no halt.

| Target | Port/address | Observed result |
| --- | --- | --- |
| Meter FF | PB/40 | Acquired 2260; address/register/read ACKs |
| PD controller 01 | PB/22 | Acquired 91; address/register/read ACKs |
| BMP D0 | PB/76 | Write-address NACK; D0 not transmitted |
| BMP D0 | PB/77 | Write-address NACK; D0 not transmitted |
| Accelerometer 0F | PA/19 | Write-address NACK; PA restoration matched |
| ENS220 PART_ID 00..01 | PB/20 | Acquired bytes 21 03, little-endian PART_ID 0321 |
| BMP D0 | PA/76 | Write-address NACK; PA restoration matched |
| BMP D0 | PA/77 | Write-address NACK; PA restoration matched |

Addresses, registers and register values in this table are hexadecimal. The
ENS220 bytes match the manufacturer's PART_ID table exactly (section 8.2.1 of
the linked data sheet). Together with the stock software fingerprint, this is
on-device evidence for an ENS220-compatible sensor on PB. The meter and PD reads
provide positive transport controls in the same pass. Failure at every BMP
target does not establish that a physical BMP device is absent or damaged.

PA was tested with DIEH cleared and PU0 enabled for bits 60; all three PA
restoration checks matched the saved configuration. Each later PA-before sample
again shows DIEH=60 and PU0=00. The PA accelerometer probe did not provide a
positive transport control, so this pass does not establish a working PA bus.

The normal temperature task still performs only the BMP sequence. Its counters
show 27 attempts/27 failures/zero successes, and the last transaction is an
address NACK on PB/77. No valid temperature was acquired. The firmware's missing
ENS220 acquisition path is therefore the principal explanation for N/A on this
unit. Next, implement and validate a separate ENS220 temperature path using real
measurement-ready/status and temperature data, keeping V5 Capture unchanged.

Capture initialization status/step/error are zero and all three V5 optimization
flags are enabled. SAVE generation is zero: this log confirms initialization,
not a new save or power-cycle retention test. No firmware was modified or new
image produced while analyzing this trial.

## 日本語要点

V1では17回とも有効なBMP IDの確認へ進めず、最後の1回がEEアドレスNACKと確定
しました。全17回の失敗履歴を保存した形式ではないため、「すべて同じNACK」とは
断定しません。PBへの変更だけでは解決していません。

V2はPBのメーター／PDを対照に、ENS220のPART_ID、PA加速度計ID、BMPの76/77を各1回だけ
読みます。PAは対象ビットだけを一時設定し、成功・失敗にかかわらず復元・照合します。
SYSTEMで10秒待ち、上記コマンドでJSONを保存してください。個体の実ID・温度取得と
オフラインの合成結果は分けて扱います。

実機V2ではPB/20から21 03（PART_ID=0321）を取得し、ENS220の仕様と一致しました。
メーター2260・PD 91も取得成功、BMPの76/77はPA/PBともアドレスNACKでした。
PAの復元は3件すべて一致しています。現行温度処理のENS220未対応がN/Aの主因と判断
できます。温度そのものは未取得で、次はENS220用の取得処理の実装・検証です。
