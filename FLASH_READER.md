# Read-only flash view trial V2 / 読み取り専用取得版

This firmware adds a polling USB HID feature-report transport and a bounded
CPU-mapped flash reader. **Hardware enumeration and acquisition are pending
device testing.** The read callback only loads bytes from the inherited CPU
mapping; it never changes SFC/cache/PLL settings or issues SPI commands.

取得版は、実機で確認したアドレス対応を使い、CPUから見える領域をUSBでPCへ送ります。
**実機でのUSB認識・読み出しはこれから確認する段階です。**
物理フラッシュの完全バックアップではありません。保存領域の調査用データです。

## Device trial

Candidate: `build/flash-reader/T3P_RUST_FLASH_READER_V2_3.0.3.bin`.
Use the existing firmware update procedure. Startup shows **FLASH READER V2**.
This is a dedicated acquisition application: measurement/PD routines are not
started. Disconnect power to stop it; restore the earlier SYSTEM/preflight
image through the existing updater when finished.

確認ファームを書き込んだら、PCにつないで次を実行します（リポジトリ直下）。
通常のWindows HIDドライバーを使います。

```powershell
py -m pip install hidapi
py flash_read.py --info
```

`ready` が1、`cpu_base` が201326592（0x0C000000）、`physical_base` が20480
（0x5000）、`view_bytes` が2072576（0x1FA000）なら、全取得を実行します。

```powershell
py flash_read.py --output build/flash-reader/t3p-device.mapped.bin
```

The tool finds exactly one HID with stock VID/PID 0483:5750 **and** product
string `T3P Flash Reader V2`. It refuses the stock updater/normal application
and multiple matching devices. This is a local experimental identity, not a
newly assigned public VID/PID. No USB driver replacement is required.

The output directory must already exist. Each transfer carries an address,
sequence, length and CRC32. An independent second acquisition must match
every byte before the `.partial` file becomes the requested output. A JSON
sidecar records the mapping, SFC readbacks, range and SHA-256. Existing output
files are not overwritten. Interrupted/failed acquisitions retain `.partial`;
choose a fresh output name to retry. No automatic resume is implemented.

An initial small-range test is also possible:

```powershell
py flash_read.py --start 0x100 --length 0x100 --output build/flash-reader/entry.mapped.bin
```

Offsets supplied to this tool are relative to the CPU view. Offset 0x100 is
CPU 0x0C000100 / physical 0x5100 under the observed mapping. The full view
covers physical `[0x5000,0x1FF000)`. It excludes the boot prefix and the
key_mac tail. Bytes may reflect SFC decryption; a `.mapped.bin` file must
never be sent to a programmer as a physical flash restoration image.

If the device does not appear, or MAPPING is LOCKED, retain a photo of the
reader screen. USB CONFIG=1 confirms SET_CONFIGURATION was accepted;
REQUESTS/READ ERRORS/USB RESETS/USB STALLS help distinguish transport issues.
The two display pages alternate every four seconds. The second page shows
SETUP count, the last two SETUP words (little-endian), last nonzero CSR0,
last received count, USB CON0, accumulated bus events and USB POWER.
Stalls can include unsupported OS requests and do not alone indicate a bad
flash read. No raw SPI workaround or guessed address is enabled on failure.

## Implementation and validation

- `native/t3p_flash_reader.c`: fixed 64-byte read/info protocol. Mapping is
  enabled only with valid root/header copies and the photographed base,
  offset and FlashSize boundary. Unknown commands and out-of-range reads
  are rejected. The API contains no flash write/erase callback.
- `native/t3p_flash_usb.c`: static aligned DMA buffers, EP0 setup/data/status,
  deferred address update, configuration/descriptor/HID feature requests,
  reset and setup-abort handling. EP1 IN remains NAK; data uses EP0 features.
  Device initialization follows stock-loader USB pin setup and pinned BR35
  SDK register definitions. Interrupts remain disabled and the driver polls.
- `flash_reader_source_evidence.py`: verifies stock boot and SDK archive
  hashes plus 16 stock USB instructions. SDK objects were inspected locally;
  none are linked or redistributed. Clock configuration is inherited, so
  actual clock/PHY operation remains a device-test dependency.
- `tests/check_flash_reader.py`: C protocol/bounds/init/USB-control tests,
  actual C responses decoded by Python, CRC/stale-response rejection,
  transfer retry and double-acquisition/partial-output behavior.

```text
python tests/check_flash_reader.py --cc PATH_TO_HOST_CLANG
cargo test --offline --features flash-reader
cargo build --offline --features flash-reader --bin preview
python flash_reader_source_evidence.py
python build.py --flash-reader
python preview_refs.py --flash-reader --preview PATH_TO_PREVIEW
python pack_demo.py --build-dir build/flash-reader --output-name T3P_RUST_FLASH_READER_V2_3.0.3.bin
```

Packaging compares ready/locked UI frames and the linked Pi32v2 native
protocol/mapped-byte callback against host decoding, then independently
unpacks the update and checks CRCs and unchanged non-application files.
These offline checks do not establish USB enumeration or physical flash
readback on the unit. Persistent screen capture and USB BMP export remain
separate pending work.

## V1 offline verification record (2026-11-12)

- Rust `flash-reader` feature: 31 unit and 7 capture integration tests passed.
- C: 14 existing checks plus 2 reader/USB checks passed.
- Python: 4 wire-format, corruption/retry and acquisition checks passed.
- 11 host preview images generated; the ready/locked reader frames were
  visually reviewed and matched the linked Pi32v2 renderer.
- Pi32v2 native info/read operations, actual mapped application bytes, CRCs
  and out-of-range rejection passed independently of the host C tests.
- Package independently unpacked with 4 nested CRC checks, unchanged boot
  prefix and 44 unchanged non-application files. Device execution pending.

Image size: 1,540,096 bytes.

SHA-256: `6c0e8f1b8b901766030eb52bfc319bba66178ee8493f884007568c4552efc732`

## V1 device failure and V2 correction (2026-11-12)

Windows reported an unknown USB device with device-descriptor request failure.
The user photograph showed MAPPING OK, USB CONFIG 0, REQUESTS 1, READ ERRORS 0,
USB RESETS 1 and USB STALLS 0. REQUESTS 1 and RESETS 1 are software startup
counts, so they do not establish any successful host transaction. No flash
data was acquired.

V1 disabled all USB bus/EP0 event sources and omitted USB CON0.SIEIE. V2
matches stock instruction 0x104372 by enabling CON0 bit 11 and enables reset/
disconnect and EP0 sources while retaining CPU-level interrupt disable and
polling. Host tests now assert those gates and exercise a bus reset after
initialization. V2 adds the second diagnostic page and changes the product
string/device revision so the PC can identify the new trial. The feature
wire protocol remains version 1. The lowercase script name in V1's display
was also changed to uppercase because the LCD font only supports uppercase.

V2 enumeration and acquisition still require a device test; this correction
must not be recorded as successful hardware recovery before that test.

V2 offline checks passed: 38 Rust tests, both reader/USB C executables,
4 Python acquisition tests, 16 pinned stock instructions, 12 generated
previews and three Pi32v2 reader frames plus native protocol checks.
Independent unpacking passed all four CRC checks with the boot prefix and
44 non-application files unchanged.

V2 image: 1,540,096 bytes. SHA-256:
`cc1ecf8337ed79f866b13f42ce16db07721c8c53491e41491b1d4fa72af3f2e8`.
