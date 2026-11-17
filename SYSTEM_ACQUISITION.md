# SYSTEM V2 acquisition evidence / 実取得の根拠

This firmware uses observed device reads, never values copied from a user photograph. V1's three SYSTEM pages were photographed on hardware: die ID `2260`, configuration `4127`, meter state OK and zero displayed read/sample errors. These photographs do not verify V2 sensor acquisition, temperature accuracy, long uptime or hold-to-exit behavior.

V1 の３ページは実機写真で確認しました。V2 のセンサー取得・温度精度・長時間の稼働・長押し終了を実機確認したという意味ではありません。

## Temperature

**V2 device trial, 2026-11-16:** PB address 20 returned bytes 21 03, matching
ENS220 PART_ID 0321. The meter/PD controls succeeded, while all four PA/PB BMP
76/77 probes address-NACKed. The V1/V2 temperature task supports only BMP;
the missing ENS220 acquisition path explains N/A on this unit. Actual
temperature is still unacquired. See [the V2 device result](TEMPERATURE_PROBE_V2.md#v2-on-device-result-2026-11-16)
for file hash, cache CRC, GPIO restoration and the limits of this observation.

The separate [ENS220 temperature V3](ENS220_TEMPERATURE_V3.md) candidate now
implements acquisition after verified PART_ID, using a first-initialization
reset, temperature-only single shots, configuration readback and fresh-data
checks. Its first hardware snapshot acquired 27.52 C (46 successes, one earlier
unresolved failure). A second snapshot added 1,015 successes without another
failure over 1,015.749 seconds, reaching 27.78 C. Continued acquisition across
this interval is verified. The subsequent V3 SYSTEM Capture SAVE/HID BMP export
also succeeded, showing +27.64 C; its image integrity and 3.110 s SAVE were
checked. This is the recorded trial scope, not a new power-cycle retention claim.
The original V5 image
and the BMP/V1/V2 paths retain their previous behavior.

[Capture V6.1](CAPTURE_V6_1.md) retains three SYSTEM pages and cached temperature
JSON without the comparison matrix. V6's hardware trial saved/exported Capture
successfully but acquired no temperature (16 attempts, 15 failures, 60 bus
transactions). The archived board loop reproduces those counts when a repaint
exceeds the 200 ms pending deadline. V6.1 services ENS220 between LCD strips
with fresh gates/time. Its initial hardware trial acquired 28.44 C with 21
successes and zero failures, verified configuration mask 0F and idle cleanup,
then saved/exported a SYSTEM BMP showing +28.38 C. That single SAVE took
3.111 s with all V5 optimizations enabled. This is the recorded initial V6.1
temperature/Capture scope; earlier V3 interval and V5 retention evidence remain
attached to their original images.

**2026-11-16 correction:** the BMP280 ID call chain uses **PB2 SCL / PB1 SDA**, shared with the meter/PD bus. The previous association with PA6/PA5 was incorrect. The PA table at `0C111A10` is a separate hardware-I2C configuration, not the route used by the BMP ID helper. See [TEMPERATURE_DIAGNOSTICS.md](TEMPERATURE_DIAGNOSTICS.md) for byte-verified call edges and the separate diagnostic candidate. Released Capture V5 artifacts remain unchanged; their old temperature implementation still uses PA and has not acquired a verified temperature.

The stock 3.0.3 app SHA-256 is `ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c`. The BMP ID read at `0C0E6D84` calls `0C0CF502`, whose START and byte operations call `0C0EA5E6` and `0C0EA64C`. Software-I2C initialization at `0C0EA578` copies table `0C11BCFC`: `(0,18,17,0x11,0,400000)`, and explicitly configures port 1 (PB), masks 4/2, input with internal 10k pull-up. Sensor initialization checks wire address `EE` (7-bit `77`), register `D0` and ID `58`, then reads 24 calibration bytes from `88`. After ID and calibration, stock writes reset `E0=B6`, control `F4=B3`, and configuration `F5=14`; the diagnostic candidate retains the existing temperature-only forced conversion policy.

V2 reads the first six calibration bytes (T1/T2/T3) and requests temperature x1 / pressure skipped / forced mode with `F4=21`. It does not copy the stock pressure/normal-mode configuration and does not reset the sensor. Conversion status `F3` must be clear and raw temperature `FA..FC` must not be the skipped-measurement code. Compensation follows the [Bosch BMP280 data sheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf), with 64-bit intermediates to avoid overflow on bad calibration inputs. Invalid ID/calibration, I/O failure, timeout, unavailable raw data, range failure or stale data gives N/A. A valid 0.00 °C remains a value.

The scheduler runs at most one attempt per second, only on SYSTEM and outside active PD capture / armed CHARGER operation. Pending conversions are abandoned on leaving that state. The V1 diagnostic candidate uses the existing meter PB GPIO setup and TIMER1 fast-bus delay, leaving PA unchanged; V2 temporarily configures and restores PA during its finite probe matrix. If fast-timer setup failed, acquisition is disabled (N/A), with no slow fallback. Positive and negative compensation, error paths, GPIO preservation, bus framing and display are tested offline. The BMP path remains unverified on this unit. The optional ENS220 V3 path now has the initial hardware temperature result above; continued acquisition and candidate Capture SAVE checks remain.

2026-11-16訂正：純正BMPの照会経路はPB2/PB1です。以前のPA6/PA5との対応付けは誤りでした。修正は温度診断候補のみで試し、完成済みCapture V5配布物は保持します。個体のID・校正値・生値・温度の実取得は未確認です。失敗・未取得・古い値はN/Aを維持します。

## Serial

The stock loader SHA-256 is `53b7b7f6ec1711becb538625a5afe4b45cc3dba5fafee8bcb229bd12d0791ec1`, with code payload base `00102600` after its 16-byte header. At `00104EF0`, the USB handler uses state base `00109B40`. The serial descriptor path at `0010530E` reads four bytes from state offset `F4` (`00109C34`), assembles a big-endian value and formats six decimal characters at `00105332..001053C8`. Its declared descriptor size is `22` hex. The most significant digit uses a fallback of `9` above `999999`; this is not enough evidence that the number is a globally unique factory serial. The product descriptor separately contains `ATORCH T3P V1.0.1`.

The producer/lifetime and validity of that updater state during normal application handoff are not established. This address lies within the application's RAM allocation; preservation across startup has not been established. V2 therefore does not read an assumed serial from that address or copy a reported number into firmware. SERIAL stays N/A until a validated normal-boot acquisition route exists. `V1.0.1` is not assigned to METER FW.

シリアルの６桁生成と更新用 USB の版文字列は追跡できました。ただし、その元データの通常起動時の所在・有効性は未確定です。更新中の RAM アドレスを通常起動の製造番号として流用せず、SERIAL は N/A を維持します。ユーザー申告の番号を固定表示する処理はありません。

## Reproduce

Run `python system_source_evidence.py --bsp PATH_TO_DOOM_ON_T3P`. It checks both stock image hashes, both I2C tables, 42 disassembled instructions, 11 resolved BMP call edges and four secondary sensor call edges against their original bytes, writing `build/system/acquisition-evidence.json`. The secondary route has an ENS220-compatible software fingerprint; see [TEMPERATURE_PROBE_V2.md](TEMPERATURE_PROBE_V2.md) for the separate ID probe. All work is offline; the script sends no USB/flash commands. `python tests/check_system.py --cc PATH_TO_HOST_CLANG` runs the C checks. Packaging also executes the native compensation function and SYSTEM display cases on the Pi32v2 simulator.
