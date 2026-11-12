# Persistent screen capture / 画面キャプチャ

`--capture-device` (Capture V1) implements a native flash backend, a frozen-frame
save gesture and BMP export through the V3 EP0 USB HID transport. It includes
SYSTEM V2 and the existing CHARGER V5 functions. The current candidate is for
hardware validation: **boot with the reduced allocation, native SPI access,
program/erase, power-off retention and Capture V1 USB export remain unverified**.

永続保存を優先する方針です。RAMだけの撮影版には変更していません。
保存処理とUSB取り出しを実装し、オフラインで検証しています。実機での成功は
候補を書き込んだ後に確認します。温度・シリアルはSYSTEM V2の取得状況のままです。

## Operation / 操作

- From any normal meter/SYSTEM/PD results page, hold the left button for **3 seconds**.
  A completed frame uses one frozen snapshot, 240×240 RGB565, with all 16 pixel bits.
- `SAVING - PLEASE WAIT` precedes storage work. `SAVED TO FLASH` and
  `SCRN0001.BMP` appear only after payload/header readback and final commit readback.
  The result stays visible for four seconds, then the meter resumes.
- Existing long-press reset/exit actions run **on release after 1 second but before
  3 seconds** in this build. Tap/double tap keep their page actions. A held boot
  button cannot cause a save. Other build variants retain the original hold timing.
- Capturing is refused during PD listening, armed CHARGER work or pending restore.
  This avoids interrupting time-sensitive protocol traffic. Measurement pauses
  during synchronous saving. No high-voltage request or active EPR entry is added.
- Six 128 KiB slots are available. Full, dirty or unknown slots are never silently
  overwritten. There is no automatic recycling or erase-all command. An interrupted
  save can consume a slot; export existing images before firmware updates, which
  can replace the capture extent.

左ボタンを3秒長押しして保存します。1秒以上3秒未満で離すと、従来のリセット・退出操作です。
`SAVE FAILED / LOCKED` は成功扱いにしません。`STORE`・`JEDEC`・`SFDP` は診断用の16進値です。
満杯や失敗した枠は自動消去しません。保存中はUSB取り出しを開始せず、完了表示を待ちます。

## USB export / USB取り出し

Install Python's `hidapi` package, connect the HID port to the PC and run:

```text
python capture_export.py --info
python capture_export.py --output build/captures/session-01
```

The device identifies as `T3P Capture V1` (VID/PID 0483:5750). Only this product is
selected; the stock software and Flash Reader V3 are not treated as capture devices.
The protocol has INFO and bounded READ only: **no host program/erase command**.
Each committed image is read twice, checked against its stored CRC32, decoded and
published as a top-down 16-bit RGB565 BMP plus a SHA-256 JSON sidecar. Existing
output names are never replaced; choose a new session directory. A failed or
changed acquisition is not published as a valid BMP. There is no invented timestamp.

Windowsのドライブ表示ではなく、専用ツールでBMPを取り出す方式です。
V3で認識・読み出しを確認したHID経路を共用します。USB経由の書き込み・消去はできません。
`src/capture/disk.rs` の読み取り専用FAT16生成部も残していますが、この候補はUSB
マスストレージとして列挙しません。FATイメージの検証と実機USBの動作は別の検証です。

## Allocation from acquired device data / 実機データと保存領域

The user's V3 CPU mapped view was acquired twice with every byte matching:

- View length: 2,072,576 bytes, physical mapping `0x5000..0x1FF000`.
- SHA-256: `ecbbe8bfbb9dfc07f40650d6435a8015e63c4e619394218a434f8d77f22be274`.
- CPU mapping: `0x0C000000`, SFC offset `0xF4005000`.
- The view excludes the boot prefix and key_mac tail; it is **not a raw or full
  physical flash backup**. Private binary inputs remain under ignored `build/`.

Blank bytes alone do not establish ownership. The old `app.bin` allocation ends
at physical `0x16AE40`; its unused portion is still covered by the old app and
area CRCs. Capture V1 explicitly reduces those allocations and inserts a reserved
JLFS extent so original resources keep their positions.

| Physical interval (end exclusive) | Capture V1 owner |
| --- | --- |
| `0x000000..0x005000` | Existing protected boot prefix, unchanged |
| `0x005000..0x085000` | Reduced app area: directory + 524,032-byte app allocation |
| `0x085000..0x16AE40` | New reserved JLFS extent; skips to unchanged resources |
| `0x090000..0x150000` | Capture storage: 786,432 bytes, 6 × 131,072-byte slots |
| `0x16AE40..` | Existing resources/remaining data, not capture storage |

The usable capture interval lies entirely inside the old app file and was FF
throughout in the acquired decoded view. This does **not** claim raw erased flash.
VM (`offset=0x178000, size=0x7A000`), PRCT (`0, 0x178000`), BTIF
(`0x1F2000, 0x1000`) and EXIF (`0x1F3000, 0x1000`) directory records remain unchanged.
The mapping/ownership of VM is not needed to authorize capture writes.

`capture_layout.py` keeps the original container length, app entry address, flags,
reserved fields and existing resource offsets. A CRC-protected private manifest
occupies unused inner-directory padding at physical `0x50C0`. Both application
CRC spans end at `0x85000`. Packaging checks them again after changing **every
capture byte**, then independently unpacks the update and compares the boot files
and all 44 non-app files. This is offline evidence; hardware boot validation of
this new layout is still required.

```text
python capture_partition_audit.py --device-view build/flash-reader/device-v3-full.mapped.bin
```

## Native storage constraints / ネイティブ保存処理

The native backend first checks the actual SFC mapping, reduced app headers,
reserved-extent header and private manifest. It refuses unsupported continuous
or DTR mode. JEDEC and SFDP are read from the chip; the SFDP gate requires a
2 MiB density, three-byte addressing support, 256-byte pages and the advertised
4 KiB erase opcode 20h. Unsupported chips stay LOCKED. Synthetic preview JEDEC
values are never used as live data. `STORE=1` indicates a mapping/layout guard,
`2` a controller/chip-profile guard, and `3` an I/O/readback fault.

SFC disable/resume and SPI byte sequencing were independently transcribed from
stock/SDK register evidence (pinned SDK revision `d0167685d032d745d88fe50233302edd46941622`,
`cpu.a` SHA-256 `787b6fc0913a0a8a634ee8c7606d5e4a68dc5604d8feacf806fd60dc1e2fc8ca`).
No SDK object or assumed ROM function address is linked. The SPI execution island
runs from owned SRAM while XIP is unavailable. Build checks reject references from
that island to flash code/constants. The standalone startup keeps CPU interrupts
disabled and does not launch a second core. The existing SFC offset/key/decode
configuration is preserved; capture reads/programming use raw SPI, so transparent
SFC decoding cannot corrupt screenshot bytes. No stale mapped capture data is used
for readback/export, and application instructions are never modified.
TIMER0 is also polled from the SRAM island during storage work, using the BSP's
existing counter conversion, so long erases/reads do not lose uptime at a 16-bit wrap.
The NOR model uses independently stated SPI0 (`0xFE0700`) and PORTF (`0xFE1000`)
addresses, checks the SPI enable bit, covers both chip-select ports and verifies
clock accumulation across counter wraps.

A SAVE initializes at most one slot whose **entire decoded image** was verified
as fresh packaged FF at boot. Erase/program addresses are bounded to the capture
partition; 256-byte page splits and one-way bit transitions are checked, every
program is read back, and the journal commits last. Unknown/partial records are
preserved. Flash status/protection registers are never written, and there is no
chip erase or VM access. Protected writes fail readback rather than being reported
as success. USB reads cannot flush a pending program buffer.

SFDP interpretation follows the primary Linux SPI-NOR definitions for
[BFPT density, erase types](https://github.com/torvalds/linux/blob/master/drivers/mtd/spi-nor/sfdp.c)
and [page size/address bytes](https://github.com/torvalds/linux/blob/master/drivers/mtd/spi-nor/sfdp.h).
Native hardware sequences remain a trial on this unit even with these gates.
A controller failure can prevent return to XIP; do not interpret a frozen SAVING
screen as a completed record.

## Build and offline checks

```text
cargo test --offline --features capture-device
python tests/check_capture_device.py --cc PATH_TO_HOST_CLANG --device-view build/flash-reader/device-v3-full.mapped.bin
python tests/check_flash_reader.py --cc PATH_TO_HOST_CLANG
python capture_store_source_evidence.py
python tests/check_system.py --cc PATH_TO_HOST_CLANG
cargo build --offline --features capture-device --bin preview
python preview_refs.py --capture-device --preview PATH_TO_PREVIEW_EXE
python build.py --capture-device
python pack_demo.py --build-dir build/capture-device --device-view build/flash-reader/device-v3-full.mapped.bin --output-name T3P_RUST_METER_CAPTURE_V1_3.0.3.bin
```

Packaging runs the full Pi32v2 measurement/PD/CABLE/CHARGER/SYSTEM comparisons,
five capture-result views and the linked native Rust save path against a NOR model.
The resulting BMP must match all 57,600 frozen RGB565 pixels. C checks separately
exercise the physical command model and USB framing; they do not emulate actual
flash silicon, cache timing or power loss on the hardware. Rust journal tests
include interrupted writes at every byte of a complete solid-image save, prior
image preservation, corrupted data, occupied slots, full storage and high entropy.

The target writer also produces `OFFLINE-native-capture-io.bin`. Replay its exact
read/program/flush sequence through the actual C page-buffer backend with:

```text
python tests/check_capture_device.py --cc PATH_TO_HOST_CLANG --native-io build/capture-device/OFFLINE-native-capture-io.bin
```

This checks the boundary between the Rust journal and C buffering, including
readback flushes and final commit. The constructed frame uses 4,352 storage calls
and 103 page-program operations in the model (maximum four per page). These are
offline counts, not measurements of the device.

## Capture V1 validation record (2026-11-12)

- Rust: 41 unit tests and 7 journal/export integration tests passed.
- C: 6 capture checks, 14 SYSTEM/baseline checks and 2 reader/USB checks passed.
- Python: 4 capture/export checks and 4 reader checks passed.
- Host: 63 reference frames generated and the capture UI reviewed visually.
- Pi32v2: capture results, linked writer, measurement, graphs, PD/EPR/history,
  CABLE, CHARGER and SYSTEM comparisons passed. The saved BMP matches all
  57,600 frozen RGB565 pixels. Its exact I/O trace also passed the C NOR replay.
- Packaging: independent full decryption, both mutated-capture CRC checks,
  unchanged boot prefix and 44 unchanged non-app files passed.
- Native SPI execution, erase/program, reduced-layout boot and power-off
  retention on the device remain pending. No device was flashed by these checks.

Candidate: `build/capture-device/T3P_RUST_METER_CAPTURE_V1_3.0.3.bin`
(1,540,096 bytes).

SHA-256: `f8bd47cab034b9eb0887d6c5837620cfdee7808d9447015d583312d76ca7aa06`.

For the earlier read-only stages and actual device observations, see
[FLASH_READER.md](FLASH_READER.md), `tests/capture_preflight_device.words` and
`tests/flash_reader_v2_device.words`. The existing `--capture-probe` firmware remains
read-only and is separate from Capture V1.
