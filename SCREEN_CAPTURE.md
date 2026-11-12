# Screen capture development / 画面キャプチャ開発

The [read-only USB acquisition trial](FLASH_READER.md) now implements a bounded
CPU-mapped view transport for partition research. Its hardware test is pending;
it does not yet provide raw whole-flash backup or persistent screenshot saving.

Status: **persistent capture is not yet available on the device**. The user
selected persistent storage, not a RAM-only replacement. This branch contains
a tested portable image journal/export core and a **read-only storage preflight
firmware**. The preflight has no flash program/erase or USB export backend.

現時点では実機の撮影・永続保存・USB取り出しは未実装です。RAM保存への方針変更はしていません。
`--capture-probe` は保存先の特定に必要な実機情報を表示する確認版です。

## Preflight trial

Build with `python build.py --capture-probe`, generate matching host references
with `python preview_refs.py --capture-probe --preview PATH_TO_PREVIEW`, then run
`python pack_demo.py --build-dir build/capture-probe --output-name T3P_RUST_METER_CAPTURE_PREFLIGHT_V1_3.0.3.bin`.
The host preview must be built with Cargo feature `capture-probe`.

On startup, **CAPTURE PREFLIGHT V1** shows four pages. Tap (or double tap) to
advance; hold to leave the preflight and start the existing SYSTEM V2 measurement
application. Reconnect power to reopen preflight. Meter/PD initialization occurs
after leaving preflight. The preflight is not a meter screen and does not sample
electrical measurements. Existing measurements are nominal/uncalibrated as before.

確認版は起動直後に4ページを表示します。短押しで次ページ、長押しで通常の測定画面に進みます。
**4ページすべての実機表示が次の調査に必要です。** この時点では撮影保存はできません。
`SAVE BACKEND: LOCKED` は実装状態を示し、フラッシュチップの保護ビットの測定値ではありません。

All numeric values are hexadecimal. ENTRY values are saved R0/R1/R2/IRQ state;
CONTEXT and AUX are opaque bootloader-provided words, not assumed capacities,
serial numbers or partition addresses. BOOT FLAGS bit 0 means the root copy was
valid; bit 1 means the context[2] auxiliary copy succeeded; bit 2 means the
context[0] header copy succeeded. Invalid fields display N/A.
Both copy-success flags are stored separately so a failed copy cannot be
shown as a measured zero. The linker audit accounts for the additional 40 bytes.
Stock entry calls the helper at `0x0C105564`; instructions `0x0C105572..0x0C10557A`
load context[0] and read offsets 8 and 13. HEADER preserves its first 32 bytes
only if that pointer falls wholly inside main SRAM. It is not a blind flash read.

The seven SFC reads are CON0, CON1, OFFSET_ADR, NDEC_SADR, NDEC_EADR, CLK_CON,
and DTR_CON at `0xFE0200 + {0,4,16,24,28,32,36}`. The register block/offsets are
from the pinned BR35 SDK `interface/driver/cpu/br35/asm/br35.h`; stock 3.0.3
also accesses this controller (for example `0x0C033D62`). The read-only probe
excludes the write-only key register and user command registers. It does not
alter SFC/cache configuration, issue SPI commands, call an assumed ROM routine,
or inspect arbitrary addresses derived from the displayed values.

## Why a fixed storage address is not enabled yet

The local stock recovery audit has not established this unit's physical flash
layout. Its loader also has a relocation path for a runtime base of `0x10000`.
The file offset in a packaged update is consequently insufficient to authorize
a physical write. VM/settings locations and encrypted versus raw flash views
also need to be reconciled before allocating a persistent partition.

The pinned SDK's `tzflash_vm_info_set` populates mapping information at runtime;
`sfc0_cpu_addr2flash_addr` calculates `cpu_address + physical_base - mapped_base`.
This is evidence that mapping must be established, not evidence that the SDK's
structure ABI matches this device's boot handoff. The probe preserves the raw
values for comparison with the stock boot/app code.

Research inputs remain local, not included in the repository:

- Stock app SHA-256: `ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c`.
- SDK revision: `d0167685d032d745d88fe50233302edd46941622`.
- SDK `cpu.a` SHA-256: `787b6fc0913a0a8a634ee8c7606d5e4a68dc5604d8feacf806fd60dc1e2fc8ca`.
- Locally inspected members: `tzsfc.c.o`, `tzflash.c.o`, `usb.c.o`,
  `usb_phy.c.o`, `usb_setup.c.o`. No SDK code/library is linked into this change.

USB controller helpers exist in the SDK, but a working independent T3P USB
device driver/descriptor/control-transfer/bulk transport path has not been
verified. The FAT sector generator alone does not make the T3P a USB drive.
The transport implementation must handle reset/error recovery according to
the [USB-IF Bulk-Only Transport specification](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).

## Portable core implemented

`src/capture.rs` accepts top-to-bottom 240x8 RGB565 strips from one frozen frame.
It preserves all 16 pixel bits with bounded literal/run encoding; even the
uncompressible frame fits in a 128 KiB logical slot. It never allocates a full
frame and never erases storage. A writer exclusively borrows its storage until
completion/abandonment; export holds an immutable borrow.

The format has a reserved header, streamed payload, CRC32/length metadata,
readback verification, and a final four-byte commit marker. Partial, malformed
or corrupt records are not exported. Dirty or unknown slots are not reused.
The current design permits 1..8 **logical** slots; this is not a claim of
available physical space or an approved address on the T3P. No clear operation
or automatic overwrite is implemented. Record sequence numbers are 1..9999.

`src/capture/disk.rs` exposes a read-only 4 MiB FAT16 *image*, with two mirrored
FATs and short filenames such as `SYST0001.BMP`. Four uppercase letters identify
the page; the four-digit counter is shared across pages. Each BMP is top-down,
240x240, 16-bit BI_BITFIELDS with RGB565 masks. File dates are the fixed FAT
epoch, not a claimed capture time. USB identity, device-side key gesture,
capture scheduling and persistent storage drivers remain to be connected.

The capture directory must remain immutable while mounted for export. A future
backend must validate the physical partition, preserve all bytes outside it,
handle page programming/RAM execution/cache coherency, and provide synchronous
read/program operations. The image core's host tests do not establish those
hardware properties.

## Validation and offline reproduction

```text
cargo test --offline --features capture-probe
python capture_source_evidence.py
python tests/check_system.py --cc PATH_TO_CLANG
cargo run --offline --features system --example capture_disk -- build/capture-fixture SYSI=build/system/preview-system-info.ppm BUSF=build/system/preview-system-io.ppm INPT=build/system/preview-system-input.ppm
python tests/check_capture_disk.py build/capture-fixture/OFFLINE-capture-fat16.img SYSI=build/system/preview-system-info.ppm BUSF=build/system/preview-system-io.ppm INPT=build/system/preview-system-input.ppm
```

The offline fixture uses synthetic host preview inputs, **not real device
captures**. An independent Python FAT walker and Pillow BMP decoder verify
directory entries, both FATs, file chains, padding and RGB565 pixel equality.
The seven journal/export tests include high-entropy pixels, interrupted writes at
every byte of a complete solid-image save, preservation of earlier captures,
unknown occupied slots, silent program corruption caught before commit,
malformed inputs, CRC failure, full storage and reversed
sector access. Fourteen C executables include exact read-address/no-write probe
checks, failed auxiliary boot copies, and startup tap/hold navigation before
measurement initialization. Packaging compares eight preflight frames (valid and
missing data) on Pi32v2 in addition to the existing SYSTEM/PD/measurement suite.

Before capture becomes usable on hardware: obtain the four preflight pages;
establish and validate the physical partition and raw/encrypted access; implement
and test the native flash and USB paths; connect the frozen-frame save gesture;
then verify repeated capture, power-off retention and byte-identical USB export
on the device. Temperature and serial acquisition remain at V2 status meanwhile.

## Preflight V1 validation record (2026-11-12)

- Rust: 41 unit tests and 7 capture integration tests passed.
- C: all 14 validation executables passed.
- Host: 66 preview images generated; three offline BMPs independently decoded
  with all RGB565 pixels matching their synthetic inputs.
- Pi32v2: eight preflight frames plus the existing measurement, PD profiles,
  EPR history, CABLE, CHARGER, SYSTEM and ten-minute graph comparisons passed.
- Packaging: independent unpacking matched the payload, four nested CRC checks
  passed, and the boot prefix and 44 non-application files remained unchanged.
- Device execution, actual persistent storage and USB export remain unverified.

Candidate: `build/capture-probe/T3P_RUST_METER_CAPTURE_PREFLIGHT_V1_3.0.3.bin`
(1,540,096 bytes).

SHA-256: `f2f941e135f34c3839154cb9c53d6f5da415a5d15c5cc5c56a6695420a362066`

## Device preflight photographs (2026-11-12)

The user supplied all four V1 pages. Their transcription is retained in
`tests/capture_preflight_device.words`. This confirms startup and display of
the preflight on this unit; it does not verify capture, writes or USB export.

BOOT FLAGS is 5: the root and root[0] header were copied successfully. AUX
is N/A because context[2] is `0x0C000000`, outside main SRAM, so the guard
correctly refused the optional RAM copy. This is not a failed flash read.

Context[1] is `0x5000`, context[2] is `0x0C000000`, and SFC OFFSET is
`0xF4005000`. These agree exactly with the SDK mapping formula modulo 2^32:
`0x5000 - 0x0C000000 = 0xF4005000`. Under this mapping, the application
entry `0x0C000100` corresponds to physical `0x5100`, matching the packaged
application offset. This is evidence for this boot, not all loader modes.

HEADER[2] is `0x1FF000`, matching the stock header's FlashSize field and
the existing recovery audit's key_mac boundary. It is not free space.
HEADER[3] decodes to bytes `21 10 A4 FF`; the SDK layout's alignment byte
is therefore 0x10 (4096 bytes), subject to stock ABI confirmation.
The reported NDEC endpoints are preserved raw; their meaning is not used
to authorize writes. No writable partition is established by these photos
alone. Remaining work is partition ownership/raw access validation and the
native flash/USB implementations. No new firmware was generated for this
evidence-only update.

## Partition ownership investigation

`python capture_partition_audit.py` reconciles the photographed mapping with
the stock 3.0.3 directory. Its output is an investigation report, never a
write authorization. The directory reserves VM with offset 1,540,096 and
length 499,712 bytes, plus BTIF and EXIF entries. Thus the space after the
distributed image cannot be assumed blank or disposable. Absolute versus
app-relative interpretation remains explicitly unresolved in the report.

An application-internal capture area would also require an explicit container
and boot-validation design: modifying bytes covered by the application CRC
cannot simply be assumed bootable. Neither region is enabled for writes.
A full device flash backup, if available, is useful evidence beyond the
distributed recovery image; blank bytes alone would not establish ownership.
