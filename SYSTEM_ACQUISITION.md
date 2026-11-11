# SYSTEM V2 acquisition evidence / 実取得の根拠

This firmware uses observed device reads, never values copied from a user photograph. V1's three SYSTEM pages were photographed on hardware: die ID `2260`, configuration `4127`, meter state OK and zero displayed read/sample errors. These photographs do not verify V2 sensor acquisition, temperature accuracy, long uptime or hold-to-exit behavior.

V1 の３ページは実機写真で確認しました。V2 のセンサー取得・温度精度・長時間の稼働・長押し終了を実機確認したという意味ではありません。

## Temperature

The stock 3.0.3 app SHA-256 is `ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c`. Its sensor I²C configuration at `0C111A10` contains SCL 6 / SDA 5 on PA, distinct from the meter/PD PB2/PB1 bus. The existing board research traces this table into GPIO initialization. Sensor initialization checks wire address `EE` (7-bit `77`), ID register `D0` and ID `58`, then reads 24 calibration bytes from `88`. The relevant instructions include `0C0E6D74`, `0C0E6D7E`, `0C0E6D8E`, `0C0E6D96`, `0C0E6DBE` and control register `F4` at `0C0E6DE6`.

V2 reads the first six calibration bytes (T1/T2/T3) and requests temperature x1 / pressure skipped / forced mode with `F4=21`. It does not copy the stock pressure/normal-mode configuration and does not reset the sensor. Conversion status `F3` must be clear and raw temperature `FA..FC` must not be the skipped-measurement code. Compensation follows the [Bosch BMP280 data sheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf), with 64-bit intermediates to avoid overflow on bad calibration inputs. Invalid ID/calibration, I/O failure, timeout, unavailable raw data, range failure or stale data gives N/A. A valid 0.00 °C remains a value.

The scheduler runs at most one attempt per second, only on SYSTEM and outside active PD capture / armed CHARGER operation. Pending conversions are abandoned on leaving that state. Sensor reads/writes share the diagnostic counters but use separate PA pins and the existing TIMER1 fast-bus delay. If fast-timer setup failed, acquisition is disabled (N/A), with no slow fallback. Positive and negative compensation, error paths, GPIO preservation, bus framing and display are tested offline. The actual sensor on this unit and its calibration still require a V2 hardware trial.

純正と同じセンサーバス・ID・校正値の経路を使用します。生値をそのまま ℃ と扱わず、温度校正値で補正してから表示します。BMP TEMP は気圧センサー内部の温度です。純正メイン画面の温度と同じ取得元であることや、室温としての精度は未確認です。失敗や古い値は N/A とし、実機で実際に読めるかは V2 の試験対象です。

## Serial

The stock loader SHA-256 is `53b7b7f6ec1711becb538625a5afe4b45cc3dba5fafee8bcb229bd12d0791ec1`, with code payload base `00102600` after its 16-byte header. At `00104EF0`, the USB handler uses state base `00109B40`. The serial descriptor path at `0010530E` reads four bytes from state offset `F4` (`00109C34`), assembles a big-endian value and formats six decimal characters at `00105332..001053C8`. Its declared descriptor size is `22` hex. The most significant digit uses a fallback of `9` above `999999`; this is not enough evidence that the number is a globally unique factory serial. The product descriptor separately contains `ATORCH T3P V1.0.1`.

The producer/lifetime and validity of that updater state during normal application handoff are not established. This address lies within the application's RAM allocation; preservation across startup has not been established. V2 therefore does not read an assumed serial from that address or copy a reported number into firmware. SERIAL stays N/A until a validated normal-boot acquisition route exists. `V1.0.1` is not assigned to METER FW.

シリアルの６桁生成と更新用 USB の版文字列は追跡できました。ただし、その元データの通常起動時の所在・有効性は未確定です。更新中の RAM アドレスを通常起動の製造番号として流用せず、SERIAL は N/A を維持します。ユーザー申告の番号を固定表示する処理はありません。

## Reproduce

Run `python system_source_evidence.py --bsp PATH_TO_DOOM_ON_T3P`. It checks both stock image hashes, the sensor bus table, and 16 disassembled instructions against their original bytes, writing `build/system/acquisition-evidence.json`. All work is offline; the script sends no USB/flash commands. `python tests/check_system.py --cc PATH_TO_HOST_CLANG` runs the C checks. Packaging also executes the native compensation function and SYSTEM display cases on the Pi32v2 simulator.
