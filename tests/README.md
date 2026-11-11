# Replay fixtures / リプレイ用データ

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

The offer from `pd_offer.hex` is supplied first as phase 0. Host and target replay exercise each following phase separately. Tests also generate matching and mismatched GoodCRC, optional-query rejections/timeouts and malformed replies.

全６件とも作成したパケットです。先頭２件や拡張能力の旧データも、実測記録として扱いません。実機で確認された XID／FW／HW は `00000000`／`00`／`00` であり、この fixture の値とは異なります。表示テストの温度・電力・Revision も実機が返した結果ではありません。

## Cable fixtures

The Identity exchange used by the cable tests reproduces the decoded Identity from the user's Sumitomo/CY4500-EPR test. Additional SVID, Modes, Manufacturer Info, active-cable status and SOP″ cases include constructed inputs to exercise optional paths. Passing those cases does not establish that an active cable or second controller was tested on hardware; the READMEs record the actual observations separately.

ケーブルの Identity は住友ケーブルの試験結果に基づきます。後続照会には任意応答やアクティブケーブル・SOP″ の経路を検証する作成データも含みます。実機確認の範囲は README の記録を参照してください。
