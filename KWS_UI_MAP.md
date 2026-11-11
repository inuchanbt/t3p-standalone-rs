# KWS-X1 capture map / キャプチャとの対応表

The user's 24 BMP captures in `kws_x1_screen_capture/` are the visual reference for this port. They include full scrollable pages, not just 240×135 screenshots. The original device has three buttons; the T3P currently uses the left button: tap to advance, double press for a page's action, hold to reset or exit as indicated in the footer.

提供された全24枚を移植の基準にします。縦長の画像はスクロール全体を含みます。KWS の３ボタン操作は、現在の T3P では左ボタンの短押し・ダブル押し・長押しへ割り当てています。画面下の案内に操作を表示します。

The captures stay in the user's local reference directory; they are not bundled with the firmware or committed here. No reference screenshot is used as a runtime image.

キャプチャはローカルの参照資料として扱い、ファームウェアへの埋め込みや本リポジトリへの追加は行っていません。

| Reference | KWS contents / 内容 | T3P implementation / 対応 |
| --- | --- | --- |
| `OVERVIEW02.BMP` | Voltage, current, power, peaks, protocol | OVERVIEW: same card hierarchy and colors; three decimal places retained. Protocol field currently reports capture state, not a decoded live contract. / 同じカード構成。契約内容の常時表示は未実装。 |
| `LINES03.BMP` | D+, D−, CC1, CC2 voltages | Dedicated ADC page pending. The cable reader's CC probe is only an attachment diagnostic. / 常時測定画面は未実装。ケーブル読取時の CC 診断とは別。 |
| `ENERGY04.BMP` | Ah, Wh, time | ENERGY: three cards; totals/time reset together; RAM only. / 積算・時間の３カード。長押しでリセット。 |
| `GRAPH05.BMP`, `GRAPH06.BMP`, `GRAPH07.BMP` | Power, voltage, current; NOW/MAX, scale and time range | Three GRAPH pages; 1/2/5/10-minute windows, labeled scale and line trace. / ３グラフと時間範囲。折れ線を維持。 |
| `RIPPLE08.BMP`–`RIPPLE11.BMP` | Ripple waveform, p-p/RMS, time/voltage divisions, spectrum | High-rate acquisition and RIPPLE menu pending. The 1 Hz history cannot provide these measurements. / 高速取得と RIPPLE 画面は未実装。履歴グラフからは生成しない。 |
| `PDPROFILES12.BMP` | Fixed PDO list, active selection | PD PROFILES: SPR list. EPR OFFER: extended list. Active fixed-PDO selection pending. / SPR・EPR の一覧に対応。能動的な電圧選択は未実装。 |
| `PPS13.BMP` | PPS request controls/status | Active PPS controller pending. PPS offers are already decoded in the capability list. / 提示値は解読済み。要求操作は未実装。 |
| `AVS14.BMP` | SPR/EPR AVS controls | SPR and EPR AVS advertised ranges/current/PDP decoded; active AVS controller pending. / 能力一覧の表示に対応。要求操作は未実装。 |
| `QC215.BMP`, `QC316.BMP` | QC 2/3 scan and voltage controls | D+/D− protocol controller pending. / D+/D− の制御実装が必要。 |
| `SCP17.BMP`, `FCP18.BMP`, `AFC19.BMP` | Legacy charger scans and vendor data | Protocol engines pending; CHARGER explicitly shows `QC NOT SCANNED`. / 未実装。未照会と表示する。 |
| `PD20.BMP` | PD session, request/acknowledgement/readiness timings, startup/port/PDO flags | PD SNIFF history and keepalive counters; CHARGER request, Accept/PS_RDY and per-query outcomes. Full SESSION page/timing data pending. / 履歴と照会結果に対応。SESSION 専用画面・時刻情報は未実装。 |
| `CHARGER21.BMP` | Capabilities, identity, peaks, inputs, batteries, source power/status/revision | V4: four active queries, nine pages described below. Legacy protocol scans remain pending. / ４種類の能動照会と９ページ。 |
| `CABLE22.BMP`, `CABLE22-2.BMP` | Identity, raw fields, SVIDs, modes, manufacturer, active/SOP″ status | Active cable scan and numbered detail pages. Only advertised optional queries are sent. / 能動読取と詳細ページ。対応を示すケーブルに追加照会。 |
| `SETTINGS01.BMP` | Brightness, rotation, totals/timer policy, calibration | SETTINGS menu pending. Existing totals use elapsed time and RAM storage. / 設定画面は未実装。現在は経過時間・RAM 保存。 |
| `SYSTEM23.BMP` | Versions, temperature, bus/frame/button/stack/reset/flash diagnostics | SYSTEM menu pending. Build/audit reports are development artifacts, not live device readings. / 実機診断画面は未実装。ビルド情報を測定値として代用しない。 |

## CHARGER V4

After the fixed 5 V contract, queries follow the KWS source-information categories:

| Query | Reply | Pages |
| --- | --- | --- |
| Get Source Cap Extended | Source_Capabilities_Extended | IDENTITY, PEAKS AND MAKER, SOURCE LIMITS |
| Get Status | Status | POWER STATUS, STATUS DETAILS |
| Get Source Info | Source_Info (one or two data objects) | POWER STATUS, SOURCE INFO |
| Get Revision | Revision | IDENTITY, SOURCE INFO |

The full page order is SCAN, CAPABILITIES, IDENTITY, PEAKS AND MAKER, POWER STATUS, STATUS DETAILS, SOURCE LIMITS, SOURCE INFO, QUERY RESULTS. Each optional query has its own outcome. An unsupported field is not rendered as a measured zero. Source Info's present/reported power is labeled as reported data; the meter reading remains in SCAN/OVERVIEW.

固定 5 V の契約後に上記を順に問い合わせます。短押しで９ページを巡ります。未対応の項目を実測ゼロとして表示せず、照会ごとの成否も確認できます。Source Info の電力は電源の報告値であり、SCAN／OVERVIEW に表示するメーターの測定値とは別です。

No EPR entry or higher-voltage request is part of this CHARGER scan. The EPR OFFER page still captures offers prompted by another sink. Adding active voltage controls, legacy scans, line ADCs and ripple acquisition requires separate board work and verification.

この CHARGER スキャンで EPR への移行や高電圧要求は行いません。EPR OFFER は引き続き外部 SNK が引き出す提示を受信します。能動的な電圧操作、旧規格の検出、ライン電圧やリップルの取得は、今後の実装・実機検証の対象です。

## References / 参照

- [KWS-X1 Standalone RS](https://github.com/kralonur/kws-x1-standalone-rs)
- [USB Power Delivery specifications](https://www.usb.org/document-library/usb-power-delivery): the local Revision 3.2, Version 1.2 specification was used for SCEDB, SDB, SIDO and RMDO layouts.
- [FUSB302B data sheet](https://www.onsemi.com/pdf/datasheet/fusb302b-d.pdf)

See [README](README.md), [日本語 README](README.ja.md) and [fixture provenance](tests/README.md) for operating instructions and validation boundaries.
