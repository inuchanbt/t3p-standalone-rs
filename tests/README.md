# Replay fixtures / リプレイ用データ

## Capture V1

Capture V2 adds the manufacturer's P25Q16H V2.1 nine-DWORD fixture
(`puya_p25q16h_sfdp.h`), rejects mutation of every published header/BFPT byte and
wrong IDs, keeps all-zero SFDP locked, and distinguishes cache/SFC/SPI timeouts.
The NOR model enforces separate SFC bit-1 then bit-0 resume writes. USB checks
cover cached diagnostics while locked and zero SPI/export-read activity, including
the host's CRC/sequence/length validation and unavailable fields represented by
`null`. These are offline models; the V1 photo/USB fields do not establish which
timeout or signature condition occurred on the chip.

`python tests/check_capture_probe_pi32.py` additionally runs the linked C guard
and USB diagnostics on Pi32v2 in six acceptance/refusal cases. Only the read-only
SPI transaction results are substituted; this is an ABI/compiled-guard check,
not a silicon or cache-timing test. Run the C fixture generator and native build first.

`check_capture_device.py` builds six C checks: the NOR/controller model, capture-button timing, capture HID protocol, existing button behavior, the shared V3 EP0 transport in capture mode, and the complete board capture loop. The board loop checks snapshot stability, refusal during PD/restore work, full/locked/failed results and return to measurement. The NOR model exercises separate SPI transactions, WEL, page boundaries, raw versus decoded blank data, partition bounds, readback, reboot selection, unknown SFDP, corrupt descriptors and protected flash. No physical device is opened. Python checks validate CRC/sequence/view rejection and independently decode BMP pixels with Pillow; partial commits and changed second acquisitions are not published. `--device-view` additionally checks the SHA-256 and headers of the local twice-acquired V3 view. That private input remains ignored.

Capture status previews use constructed `EF4015`/`SFDP` values. **These are not this unit's measured JEDEC/SFDP values**, and are never supplied to the live storage driver. The Pi32v2 check exercises the linked Rust writer against a NOR model at its C storage boundary, then checks the BMP against all 57,600 RGB565 pixels of the frozen host frame. Physical SPI framing is covered separately by the C model; neither test establishes hardware programming or power-off retention.

撮影の検証画像・JEDEC値は作成データです。取得済みの実機データは保存領域とマッピングの確認に使い、書き込み処理の実機成功を示すデータとは区別します。

These files contain FUSB302-style RX records: the SOP token, little-endian PD header, data-object-aligned payload and CRC32. They are test inputs, not a substitute for a CY4500-EPR recording. Files in the user's `xy4500_epr_log/` directory remain local and are not committed.

SOP トークン、PD ヘッダー、データオブジェクト境界に揃えたペイロード、CRC32 を並べた検証用データです。CY4500-EPR の記録そのものではありません。ユーザーのログ保存先はローカルに残しています。

## `charger_scan.hex`

All six records are **constructed fixtures**, including the Accept, PS_RDY and Extended Source Capabilities records that predate V4. They are not bytes copied from `CHARGER_02.ccgx3`.

| Line | Phase | Contents |
| --- | --- | --- |
| 1 | 1 | Accept, source message ID 1 |
| 2 | 2 | PS_RDY, source message ID 2 |
| 3 | 3 | Source_Capabilities_Extended, source ID 3; VID/PID `32AC/0022`, constructed XID `01020304`, FW `01`, HW `02`, SPR PDP 100 W, EPR PDP 240 W |
| 4 | 4 | Status, source ID 4; seven bytes `00 06 00 00 02 00 01` (temperature unavailable, AC input, normal temperature status, S0 power state) |
| 5 | 5 | Source_Info, source ID 5; SIDO1 `80F0F001`, SIDO2 `C003C1E0` (guaranteed/DPS example, 240 W limits) |
| 6 | 6 | Revision, source ID 6; RMDO `32120000` (Revision 3.2, Version 1.2) |

The offer from `pd_offer.hex` is supplied first as phase 0. Host and target replay exercise each following phase separately. Tests also generate matching and mismatched GoodCRC, optional-query rejections/timeouts and malformed replies. The V5 Status-only `Not_Supported` case reproduces the HKY/Delta outcome pattern from the supplied hardware evidence using constructed packets, not bytes copied from `CHARGER_04.ccgx3` or `CHARGER_05.ccgx3`. C transaction tests also cover all queries unsupported, Reject, Wait, no reply and a Status-phase bus error.

全６件とも作成したパケットです。先頭２件や拡張能力の旧データも、実測記録として扱いません。実機で確認された XID／FW／HW は `00000000`／`00`／`00` であり、この fixture の値とは異なります。表示テストの温度・電力・Revision も実機が返した結果ではありません。

## SYSTEM diagnostics

`system_stats.words` contains 23 constructed 32-bit words (the temperature word is signed) in the order shared by `native/t3p_system.h` and `src/system.rs`. It is not a device dump. Host previews and target replay use this snapshot to check a >49-day uptime, ID/config values, meter state, bus/sample/frame counters and input diagnostics. Other constructed cases cover absent snapshots, invalid ID/config after a sample failure, maximum counters and known zero counts with unavailable frame timing. No fixture value is inserted into the device's live diagnostic path.

`tests/check_system.py` runs the C checks for clock wrap/counter saturation, recognized input events and delay, shared bus counters and errors, frame completion/failure, refresh scheduling, CHARGER and cable transactions. Rust checks also preserve diagnostic state across ENERGY reset; target replay verifies the C/Rust snapshot layout, eleven SYSTEM frames, page cycling and exit without resetting meter totals.

`system_stats.words` は C と Rust で共有する順序の32ビット値23個からなる作成データです。実機のダンプではありません。49日を超えた稼働時間、通信・取得・描画・入力の集計などを検証し、別ケースで未取得・失敗・最大値・既知のゼロを確認します。検証用の値を実機の診断経路へ組み込むことはありません。

## Cable fixtures

The Identity exchange used by the cable tests reproduces the decoded Identity from the user's Sumitomo/CY4500-EPR test. Additional SVID, Modes, Manufacturer Info, active-cable status and SOP″ cases include constructed inputs to exercise optional paths. Passing those cases does not establish that an active cable or second controller was tested on hardware; the READMEs record the actual observations separately.

ケーブルの Identity は住友ケーブルの試験結果に基づきます。後続照会には任意応答やアクティブケーブル・SOP″ の経路を検証する作成データも含みます。実機確認の範囲は README の記録を参照してください。
