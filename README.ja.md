# T3P Standalone

[読み取り専用USB取得版](FLASH_READER.md)は、V3で実機のUSB認識とCPUから見えるフラッシュ領域の二度読み一致を確認しました。完全な物理フラッシュバックアップではありません。

画面キャプチャは開発中です。画像保存形式とBMP取り出し部分はPC上で検証済みです。[保存領域の確認版](SCREEN_CAPTURE.md)は起動情報・フラッシュ設定を読み取り表示します。**実機での撮影・永続保存・USB取り出しはまだ使えません。**

[English](README.md)

ATORCH T3P USB 電力計向けの実験的な Rust ファームウェアです。[KWS-X1 Standalone](https://github.com/kralonur/kws-x1-standalone-rs) と、ローカルの DOOM on T3P 基板調査をもとに開発しています。

## 現在の状態

標準ビルドは、**模擬データを使った起動・表示デモ**です。別途、個体校正なしの実測ビルドもあります。正方形の画面を使い、KWS-X1 に寄せた **OVERVIEW** に電圧・符号付き電流・電力と最大値、**ENERGY** に Ah・Wh・経過時間、**GRAPH** に直近1・2・5・10分の電力・電圧・電流を表示します。別ビルドでは SPR と EPR の PDO を受信でき、下記の条件で両方の実機受信を確認しました。PD SNIFF を直列接続の CY4500-EPR と比較し、一致した記録と取りこぼした記録の両方を確認しました。

ENERGY 画面の時間は起動時または積算リセット後の経過時間です。測定値が取得できない間や電流ゼロの間も進みます。旧画面の実機で時間が進み Ah・Wh とともにリセットされることを確認しました。表示は `99:59:59` で止まり（実機未確認）、積算値と時間は再起動後に復元されません。PD 診断版では OVERVIEW の PROTOCOL 欄に生の診断値を表示します。

KWS 風の画面、ダブル押し、長い期間のグラフを実機で確認しました。最大値は電源断で消えます。左ボタンの短押し・離す操作で、OVERVIEW、ENERGY、電力・電圧・電流の各グラフを順に切り替えます。グラフ画面ではダブル押しで1・2・5・10分を切り替えます。OVERVIEW と ENERGY ではダブル押しも1ページ進みます。約1秒の長押しで Ah・Wh・経過時間と最大値をリセットします。PD SNIFF と CABLE では長押しで OVERVIEW に戻ります。短押しはダブル押し判定のため約350 ms 後に確定します。電流グラフは絶対値を表示します。グラフ履歴は画面間で共有し、リセットしても消えません。各最大値は異なる時点の値になり得るため、最大電圧と最大電流の積が最大電力と一致するとは限りません。

正方形向けの目盛り付き折れ線グラフは実機確認済みです。各期間を60点にまとめ、それぞれ1・2・5・10秒の平均を表示します。一部に欠測がある区間は孤立した点、全て欠測の区間は空白にします。縦軸はゼロと、単位付きの上限を表示します。上限は表示範囲の平均値の最大に合わせて 1・2・2.5・5 の段階に切り上げ、最小 1 W／V／A とします。上限が変わると線全体の高さも変わります。

共通の黒い背景、細い色枠、選択中のタイトルと隣接する色付きタブを、提供された KWS-X1 のキャプチャに合わせています。電圧は黄、電流はシアン、電力は緑、MAX の枠は紫です。240×240 の画面でも小数3桁と `UNCAL` 表示を残します。[キャプチャとの対応表](KWS_UI_MAP.md)に全24枚と、T3P での実装が残っているメニューをまとめています。

## PD PROFILES 試験版

`--pd-profiles` でビルドすると、電流グラフの次に **PD PROFILES** が出ます。左ボタンのダブル押しで CC1 の受信を10秒間待ち、5 V の試験構成で電源をつなぎ直します。HID から別給電すると、測定側の電源をつなぐ前に受信を開始できます。取得は常に CC1 を使い、再度ダブル押しすると CC1 で取得し直します。試験した個体では CC1 でのみ受信が確認されており、反対側の CC を使うケーブルの向きでは受信できない場合があります。このソフトウェア上の制限は内部配線を確定するものではありません。最初に CRC が一致した SPR の Source_Capabilities を表示し、再取得または電源断まで RAM に保持します。SPR の5件の PDO は T3P 実機で受信・表示できました。

次の **EPR OFFER** ではダブル押しで CC1 を30秒間受信します。5 V から始め、接続した ASD-PD31 などの PD シンクから EPR 情報を問い合わせるか EPR モードに入ります。再度ダブル押しすると CC1 で取得し直します。EPR_Source_Capabilities は通常2つのチャンクに分かれます。両方の CRC が一致し順に受信できたら `CAPTURED`、チャンク1だけを受信したときはその EPR PDO を `PARTIAL` と表示します。`NO EPR CAPS` は待機時間中に完全な情報を捕捉できなかったことを示します。画面には CRC が一致した SOP パケット数 `RX`、そのうち SPR Source_Capabilities の数 `SPR`、認識した EPR チャンクの数 `E0`・`E1` も各最大255件まで表示します。受信できた種類の切り分けに使うもので、0 件でも配線上の通信がなかったとは断定できません。

固定 PDO は電圧・最大電流・計算した電力、PPS は電圧範囲と電流上限を表示します。SPR AVS は 9～15 V と 15～20 V の電流上限、EPR AVS は電圧範囲と PDP を表示します。それ以外の PDO は生の16進数で表示します。一覧の値は電源が提示した値です。起動時の CC 設定を保ち、PD 送信と自動 GoodCRC を無効のまま受信します。FUSB302B 互換であれば内部の両 CC プルダウンは有効で、**高インピーダンスのパッシブスニファーではなく、別のシンクとの接続に影響する可能性があります。** EPR 情報は別のシンクが促した通信から取得します。シンク接続時の完全な EPR 受信を1例確認しており、他の接続条件や向きは引き続き検証が必要です。 [FUSB302B データシート](https://www.onsemi.com/pdf/datasheet/fusb302b-d.pdf) · [USB Power Delivery 仕様](https://www.usb.org/document-library/usb-power-delivery)

## PD SNIFF 試験版

次の **PD SNIFF** 画面では、ダブル押しから30秒間、CC1 で CRC が一致した SOP メッセージを記録します。同じ画面で再度ダブル押しすると CC1 で新しい記録を始め、それまでの履歴は消えます。履歴は RAM のみに最大192行を保持します。まず新しい8件を古い順に表示し、短押しで8件ずつ過去に戻り、最も古いページでもう一度短押しすると CABLE に進みます。約1秒長押しすると積算値をリセットせずにすぐ抜けられます。たとえば `R:185-192/192` は保持中の192件のうち表示している範囲です。満杯になると古い記録から上書きされます。受信中は新着により表示範囲がずれるため、古い履歴は `DONE` 後の確認を推奨します。`SRC` / `SNK` は各メッセージの電源ロールビットに基づく表示であり、物理的な送信元を独立に確認したものではありません。Source Capabilities、Request、Accept、PS_RDY、EPR などの主な種類を名前で示し、未対応の種類は `CTRL`・`DATA`・`EXT` と種類番号 `T` で表示します。複数ページの履歴を実機で確認しました。192件を超えたときの上書きと長押しでの終了は実機未確認です。

データオブジェクトが1つの EPR_Mode は `EPR_ENTER`、`EPR_ACK`、`EPR_OK`、`EPR_FAIL`、`EPR_EXIT` に分けます。未知のアクションは `EPR_Axx`、`Ixx` はメッセージ ID、`Dxx` はアクションに付随するデータの生値を16進数で表示します。Enter のデータはシンクの Operational PDP、Enter Failed では `D00` が原因不明、`D01` がケーブルの EPR 非対応、`D02` が VCONN 失敗、`D03` が RDO の EPR 対応ビットなし、`D04` が電源側の一時的な失敗、`D05` が PDO の EPR 対応ビットなしを表します。その他の値は生値のままです。個々のメッセージを解読するもので、やり取り全体や実際の電圧を確定するものではありません。Enter・Ack・OK・Exit は実機表示と CY4500 の記録が一致しました。Fail と未知のアクションは実機未確認です。 [EPRMDO 定義](https://chromium.googlesource.com/chromiumos/platform/ec/+/6e6e567deac5b5ab8fbe0d706cd82ebeb21e35b4/include/usb_pd.h)

シンクの EPR KeepAlive と電源の KeepAlive_ACK は画面下に別々に集計します。この2種類と GoodCRC は履歴の行を使いません。`MSG` は KeepAlive と ACK を含む、GoodCRC 以外の CRC が一致した SOP メッセージの総数です。`GCRC` は受信した GoodCRC の総数です。各カウンターの表示は999までです。 [Extended Control の定義](https://chromium.googlesource.com/chromiumos/platform/ec/+/6e6e567deac5b5ab8fbe0d706cd82ebeb21e35b4/include/usb_pd.h#667)

行末の `N` はデータオブジェクト数、`P` は要求した PDO の位置、`C` は拡張メッセージのチャンク、`Q` はチャンク要求、`I` はメッセージ ID です。`DONE` は待機時間の終了を表し、全パケットの受信や契約成立を保証しません。EPR と SNIFF の受信処理は通常最短1 ms間隔で確認し、溜まった記録は1回の処理で最大4件まで続けて読み出します。1件以上受信した直後は通常の待機を1回省いて再確認し、追加の受信がなければ通常の待機に戻ります。実際の間隔は I²C や画面描画にも左右されます。読み出しで消える FIFO アラートは受信を継続しつつ `WARN` として保持します。FUSB302B 互換ならこのアラートは RX または TX の満杯を示しますが、それだけで必ずパケットが失われたとは断定できません。RX 満杯の状態を実際に読み取った場合は `FIFO FULL` で停止します。不正なフレームやリンクリセットでも停止して状態を表示します。特に短い間隔の通信は取りこぼす可能性があり、契約電圧や電流の推定は行いません。CC プルダウンの設定は上記と同じです。 [FUSB302B データシート](https://www.onsemi.com/pdf/datasheet/fusb302b-d.pdf)

警告は SPR・EPR・SNIFF・CABLE の結果ごとに RAM 内で保持し、同じ種類の新しい取得を始めるか電源を切ると消えます。

現行の受信優先試験版では、EPR OFFER・PD SNIFF・CABLE の取得中、受信以外の処理を減らすため画面の定期更新を止めます。途中で別画面に移った場合も、取得終了まで表示はその時点のままです。ボタン操作時と取得終了時は再描画し、電圧・電流のサンプリングと積算は継続します。比較試験では `LISTENING` 画面が落ち着いてから接続し、取得終了までボタンを押さずに待ってください。SPR の取得中は従来どおり定期更新します。この画面更新停止を導入した前版 `PD_RX_PRIORITY` は下記の実機試験で使われましたが、定期更新停止そのものは別途確認できていません。通信の取りこぼしがなくなる保証はありません。

## CABLE 試験版

PD SNIFF の次にある **CABLE** で左ボタンをダブル押しすると、CC1 で最大30秒間待機します。HID 給電を続けたまま `LISTENING` を確認し、5 V の試験構成で e-marker 付きのケーブルを使って電源をつなぎ直します。電源側が VCONN を供給してケーブルに問い合わせます。T3P はパッシブまたはアクティブケーブルからの、CRC が一致した最初の SOP′ Discover Identity ACK を取得します。`SOP1 RX` は要求や GoodCRC も含む CRC 一致の SOP′ 受信件数で、上限は255件です。

`CAPTURED` または `CAPTURE WARN` が出たら、短押しで **SUMMARY**（概要）、**RAW**（生データ）、**FLAGS**（ビットフィールド）の順に進み、最後に OVERVIEW に戻ります。概要はケーブルの種別、申告された USB 速度と電力定格、遅延からの長さの推定、VID/PID と XID を表示します。PD 2.0 の旧 Cable VDO と、PD 3.x / SVDM 2.x のパッシブ VDO 1.0・アクティブ VDO 1.3 を解読します。ほかの形式では識別情報と生データを確認できます。ダブル押しで取得し直し、長押しで OVERVIEW に戻ります。取得結果は RAM に保持します。

住友ケーブルを使った CY4500-EPR 記録の Discover Identity を再生し、パッシブ、USB4 Gen 3、50 V / 5 A EPR、VID/PID `20C2/0A14` と表示できることを確認しました。 [USB Power Delivery 仕様](https://www.usb.org/document-library/usb-power-delivery) · [KWS-X1 の CABLE メニュー](https://github.com/kralonur/kws-x1-standalone-rs#16-cable)

CC1 の実機試験でも、T3P に同じ識別情報が表示されました。写真の PD ヘッダー `518F` と5個の VDO は、VBUS 約 5.005 V で CY4500-EPR が記録した最初の SOP′ Discover Identity ACK と一致しました。`SOP1 RX:003` は照会、その GoodCRC、識別応答の3件と整合します。`CAPTURE WARN` は、受信 FIFO の警告が立った状態で CRC が一致する識別応答を取得したことを示します。

### ケーブル単体の能動読取試験

`--pd-cable-read` 版では **CABLE** からケーブル単体を読み取れます。T3P は側面の HID ポートから給電し、測定用のメス端子に e-marker 付きのケーブルを挿します。ケーブルの反対側と T3P の測定用オス端子は空けて、**CABLE** で左ボタンをダブル押しします。短押しで SUMMARY・RAW・FLAGS と番号付きの詳細ページを順に表示します。ダブル押しで再取得し、長押しで OVERVIEW に戻ります。

最初の実機試験は、プラグの両向きで `NO RA ON CC2` で止まりました。改訂版は両方の CC を 330 µA でプルアップして調べ、検査後に読取が失敗した場合に CC1・CC2 の概算電圧を表示します。`HIGH` は測定上限の約 2.69 V を超えたこと、`LOW` は約 0.04 V を下回ったことを示します。CC1 が約 0.3 V で CC2 が `HIGH` なら、ケーブルの終端が CC1 側にある可能性があります。

VBUS を確認し、CC2 が Ra の範囲内で CC1 が開放と判定できたとき、CC2 に一時的に VCONN を供給し、CC1 で SOP′ Discover Identity を送ります。試行が終わるたびに PHY の設定を元に戻します。住友ケーブル単体での Identity 取得は実機で確認済みです（VID/PID `20C2/0A14`）。CY4500-EPR でもケーブルの Identity ACK と T3P の GoodCRC を確認しました。 [FUSB302B データシート](https://www.onsemi.com/download/data-sheet/pdf/fusb302b-d.pdf)

V3 は Identity の応答に応じて追加の問い合わせを行います。

- Modal 対応を広告したケーブルには Discover SVIDs と各 SVID の Discover Modes を送ります。最大32種類の SVID、最後の SVID 応答の生データ、各 SVID の VDO をオブジェクト位置順に表示します。SVID 一覧の続きは最大6ページまで読み、上限に達すると `LIST LIMIT` を表示します。
- PD 3.x のケーブルには Get Manufacturer Info を送ります。メーカー名とこの応答の VID/PID は Identity の値と分けて表示します。PD 3.x のアクティブケーブルには Get Status も送り、温度とシャットダウン状態を表示します。解読した Active Cable VDO が第2プラグのコントローラーを広告していれば、SOP″ を Soft Reset で同期してから、そのコントローラーにも Get Status を送ります。
- 各ページに `ACK`、`NAK`、`NOT SUPPORTED`、`NO GOODCRC`、`NO REPLY` などを表示します。ケーブルによって返す情報は異なります。後続の問い合わせが失敗しても、PHY の設定を復元できれば取得済みの情報は確認できます。 [KWS-X1 の CABLE メニュー](https://github.com/kralonur/kws-x1-standalone-rs#16-cable) · [USB Power Delivery 仕様](https://www.usb.org/document-library/usb-power-delivery)

T3P 実機で住友のパッシブケーブル単体（`20C2/0A14`）を読み取れました。SVID 一覧の `1E4E` と `8087` はどちらも Discover Modes に `ACK` を返し、モード VDO はそれぞれ `9031011C`、`00030001` でした。Manufacturer Info の応答は `1E4E/0903` と `eEver`（画面表示は `EEVER`）でした。状態画面では SOP′ が `PASSIVE`、SOP″ が `NOT PRESENT` / `NOT QUERIED` と表示されました。アクティブケーブルの Get Status と SOP″ 通信はシミュレーターで検証済みで、実機確認が残っています。

## CHARGER の 5 V スキャン

試験用の `--pd-charger` 版では **CABLE** の次に **CHARGER** が出ます。側面の HID ポートで給電し、測定用の両端子を空けた状態で **CHARGER** の左ボタンをダブル押しします。`RD OFF` が出たら電源を CC1 の向きで T3P のメス端子につなぎます。オス端子は空けたまま、30秒以内に1回短押しします。待機中は長押しで中止できます。

V5 は PDO 1 に固定 5 V を要求します。要求電流は最大 0.5 A です。Accept と PS_RDY の後、PD 3.x 電源へ **Get Source Cap Extended → Get Status → Get Source Info → Get Revision** の順で問い合わせます。各照会の前に SinkTxOK を待ち、メーターの値が 4.5〜5.5 V の範囲にあるか確認します。照会ごとの結果を別々に保存し、ACK 済みの照会が応答なし・Not Supported・Reject・Wait でも、取得済みの情報を残して次へ進みます。送信未確認やリンク・測定エラーではスキャンを止めます。PD 2.0 は `5V CONTRACT OK` と表示し、これらの PD 3.x 照会を送りません。

４種類すべてが有効なデータ応答または `Not_Supported` で終わると `CAP OK` です。任意照会への未対応通知は正常な回答として扱い、個別欄には `NOT SUPPORTED` を残して `OK` と同じ正常色で表示します。４種類すべてが未対応でも正常完了となるため、全情報を取得できたという意味ではありません。ACK 済みでも応答なし・Reject・Wait があれば引き続き `PARTIAL RESULTS`、送信未確認やリンク・測定エラーは失敗です。短押しで **SCAN → CAPABILITIES → IDENTITY → PEAKS AND MAKER → POWER STATUS → STATUS DETAILS → SOURCE LIMITS → SOURCE INFO → QUERY RESULTS** を巡り、最後に OVERVIEW へ戻ります。電源が返すピーク条件、入力電源・バッテリーのフラグ、保持時間、適合情報、現在・報告電力、温度や制限要因などを表示します。取得できない項目には未取得・未対応の状態を残します。拡張 PDP は電源が提示する能力です。このスキャンは EPR や高電圧 PDO を要求しません。EPR の提示を見る場合は EPR OFFER を使います。再試行は電源を抜いてダブル押しします。

実機では 5 V・0.500 A の要求に対して Accept と PS_RDY を受信しました。拡張照会の送信を待つ間も、メーターの測定値で 4.5〜5.5 V の範囲を確認します。V2 では拡張応答の VID/PID と PDP を正しく表示しながら、成否欄に `NO GOODCRC` が出ました。CY4500-EPR には拡張応答と電源の GoodCRC が記録されていました。V3 は送信完了フラグがなくても検証済みの拡張応答で成功と判定し、照会に対応する GoodCRC も受信 FIFO から判別します。

V3 の実機試験では `SOURCE EXT CAP OK` と 5.085 V が表示されました。直列接続した CY4500-EPR は照会（メッセージ ID 1）と応答（メッセージ ID 3）の両方に対応する GoodCRC を記録しました。25バイトの応答に含まれる VID/PID `32AC/0022` と電源が提示した PDP（SPR 100 W、EPR 240 W）は、画面の表示と一致しました。

引き継いだ実機証拠では、Chicony A240AP03Q／Framework FRANDVCH00（`CHARGER_03.ccgx3`）は４種類とも応答しました。HKY A180C-PD240W-C14（`CHARGER_04.ccgx3`）と Delta ADP-240LB B／Lenovo OEM（`CHARGER_05.ccgx3`）は Status が `Not_Supported`、ほかはデータ応答でした。V5 はこの組み合わせを `CAP OK` とします。変更後の完了表示は実機未確認です。PC テストには、この応答パターンを作成パケットで追加しています。[リプレイ用データの説明](tests/README.md)で、作成したテストパケットとアナライザーの記録を区別しています。

## SYSTEM 診断画面

`--system` 版は CHARGER V5 の機能を含み、CHARGER の次に **SYSTEM** を追加します。短押しで **SYSTEM INFO → BUS AND FRAMES → INPUT AND RUNTIME → OVERVIEW** と進みます。ダブル押しは SYSTEM 内のページ切り替え、長押しは積算・MAX・診断値をリセットせず OVERVIEW へ戻る操作です。通常の画面更新に合わせて表示します。メーター・PD の集計は既存通信を利用し、V2 では下記条件の温度照会を追加します。

V2 は別系統の PA6/PA5 センサーバスと既存の高速タイマーで **BMP TEMP** を実取得します。高速タイマーを利用できなければ N/A を維持します。SYSTEM の表示中で、PD 取得や CHARGER の待機操作がない場合だけ、BMP280 互換 ID `58` と個体の温度校正値を読み、温度のみの単発変換を要求します。センサーのリセットや圧力スキャンは行いません。変換完了後の補正値が −40〜+85 ℃の範囲にある場合だけ表示し、通信失敗・ID／校正不正・未変換・長時間の変換待ち・2.5秒を超えた古い値は `N/A` にします。これはセンサー内部の温度であり、校正済みの室温ではありません。[取得経路とシリアルの未確定事項](SYSTEM_ACQUISITION.md) · [Bosch BMP280 データシート](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf)

SPR 以外の取得中は、既存の受信優先処理に合わせて SYSTEM の定期描画も止まります。温度照会も止めますが、稼働時間と診断カウンターは継続し、次の描画で最新の値を表示します。I²C 集計には温度用の別バスも含みます。

SYSTEM INFO は、自作ソフトの版（`T3P RS SYSTEM V2`）、純正の更新コンテナーの版（`BASE IMAGE 3.0.3`）、稼働時間、最後に正常取得したメーターの die ID・設定値、直近の識別・サンプル取得結果を表示します。die ID はメーターの FW 版とは区別します。稼働時間はアプリの BSP タイマー開始から数え、LCD の初期化時間を含みます。32ビットのミリ秒時計が周回しても継続し、ENERGY のリセットとは独立します。純正ブートローダーの起動時間は含みません。

BUS AND FRAMES はメーターと PD コントローラーを合算した I²C 読み書きの試行・通信エラー、メーター取得の試行・失敗、完了した画面描画の回数を表示します。FIFO への書き込みも I²C 書き込み１件であり、PD パケットの個数ではありません。取得回数には既存の CABLE／CHARGER の確認処理も含みます。描画の直近・最大時間はスナップショット作成、描画、LCD 転送、転送間のポーリングを含む、前回までに完了した描画の所要時間です。最初の描画が完了する前は、描画回数は既知のゼロ、所要時間は `N/A` になります。INPUT AND RUNTIME は DBI 操作回数、認識した短押し・ダブル押し・長押しの回数、最後の入力イベントと認識までの時間を表示します。入力の時間は最後の生入力変化から数え、デバウンスと各操作の判定待ち時間を含みます。

追加した診断カウンターは再起動まで積算し、`4294967295` で飽和させます。既存の DBI 操作回数は32ビットで周回し、フレーム数とは別です。未取得のメーター FW・温度・製造番号・スタック使用量・リセット理由・フラッシュ診断は `N/A` です。ID・設定値の読み取りに失敗した場合も、その項目を未取得に戻します。診断スナップショットがなければ `N/A`、取得済みのゼロはゼロと表示します。これらは作成データを使った PC テストと本体向けコードのリプレイで検証します。SYSTEM V2 は実機未確認です。

## CC1 の Rd 接続試験

`python build.py --pd-rd-test` でビルドし、電流グラフの次にある **RD TEST** を開きます。HID で給電し、測定用の両端子を空けて始めます。左ボタンをダブル押しして `RD OFF` を表示させてから、動作実績のある USB-C 電源を CC1 の向きで T3P のメス端子につなぎます。反対側の測定端子は空けておきます。約2秒待ってから左ボタンを1回短押しして Rd を入れ、保存された前後の VBUS を比べます。`5V SEEN` は、Rd を切ったときは4 V未満で、Rd を入れた後は3回連続で4.5〜5.5 Vだったことを示します。

待機は Rd OFF が30秒、Rd ON が3秒です。試験中に長押しすると中止し、結果画面で長押しするとメニューに戻ります。再試行するときは電源を外してダブル押しします。起動時の設定を確認し、`SWITCHES0` だけに `04`、`05` の順で書き込み、試験終了時に `03` に戻します。`RESTORE ERROR` なら電源を外し、T3P の電源を入れ直します。VBUS は未校正のメーターの読み値です。 [FUSB302B データシート](https://www.onsemi.com/pdf/datasheet/fusb302b-d.pdf)

`PD_RD_TEST_V2` の実機試験では、電源接続済みでダブル押しすると `VBUS PRESENT` が出ました。電源を外してダブル押しすると `RD OFF` と 3.003 V が表示されました。その後 CC1 の向きで電源をつなぎ、短押しすると Rd OFF 時は 2.660 V、Rd ON 時は 5.085 V で、`5V SEEN` と `DEFAULT RD RESTORED` が出ました。電源を外した状態の写真では `NO 5V` で、両方の欄が 3.002 V でした。数値はすべて T3P の表示値です。

## CC1 の GoodCRC 試験

`python build.py --pd-ack-test` でビルドすると同じ位置に **PD ACK TEST** が出ます。接続とボタン操作は上の Rd 試験と同じです。短押し後、FUSB302B 互換のコントローラーがシンクとして、有効な SOP パケットに自動で GoodCRC を返します。3秒以内に 4.5〜5.5 V、CRC が一致する `Source_Capabilities`、送信を示す `I_GCRCSENT` の3つがそろうと `PD ACK SEEN` になります。画面には提示された PDO 数と、GoodCRC の送信を検出したかを表示します。今回は最初の提示への応答を検証します。5 V の PD 契約と情報照会は次の段階です。

V1 の実機試験では、初回は 5.085 V、PDO 5件で `PD ACK SEEN` となりました。CY4500-EPR でも最初の `Source_Capabilities` の終了から 78 µs 後に、同じメッセージ ID の SOP GoodCRC を確認しました。再試行では同じ電圧で `NO SRC CAPS` となり、アナライザーでは `Source_Capabilities` が繰り返され、SOP GoodCRC は観測されませんでした。V2 は試行ごとに `PD_RESET` でコントローラー内部の PD 送受信回路を初期化してから、受信 FIFO をクリアして GoodCRC の自動応答を有効にします。 [FUSB302B データシート](https://www.onsemi.com/pdf/datasheet/fusb302b-d.pdf)

V2 の実機試験では、繰り返し試行して毎回 `PD ACK SEEN` と `SRC PDO: 5` が表示されました。そのうち1回の再試行を CY4500-EPR で記録し、PDO 5件を載せた `Source_Capabilities`（メッセージ ID 0）の終了から 78 µs 後に、同じ ID の SOP GoodCRC を確認しました。

## 実機での検証

`PD_KEEPALIVE` 版で EPR を維持した CC1 試験では、T3P は `DONE`、`MSG:117 GCRC:117`、`EPR KEEPALIVE:045 ACK:045` を表示しました。4ページの全27行は内容と順序が CY4500 と一致しました。件数は、CY4500 の最初の SOP パケットから45往復目の ACK に続く GoodCRC までの27.47秒間と一致します。この区間の VBUS は約5 Vでした。

`PD_KEEPALIVE` 版の CC1 SNIFF は2回とも `DONE` で、全18行の内容と順序、GoodCRC 18件がそれぞれ CY4500 と一致しました。両試験でシンクは EPR 側の `PS_RDY` の約29 ms後に `EPR_EXIT` を送りました。両ログで KeepAlive と ACK は0件で、実機のカウンターもそれぞれ `000` でした。非ゼロの集計は、以前の長い CY4500 ログを PC で再生して確認しました。

EPR OFFER の2回はともに `CAPTURED` で、`RX:019 SPR:001 E0:001 E1:001` が CY4500 の後半チャンクまでの件数と一致しました。PDO 8〜11 の値も一致し、固定は 28、36、48 V で各5 A、AVS は 15〜48 V で PDP は240 W でした。4試験とも CY4500 が測定した VBUS は約5 Vでした。

追加の比較試験では、`EPR_REQUEST P01` に続く `PS_RDY` までの T3P の13行が CY4500 と一致しました。`EPR_SRC_CAP` の C00・Q01・C01 も含まれます。T3P は `DONE` で `MSG:139 GCRC:139` でした。CY4500 の139件目は最初の Source_Capabilities から29.0秒後で、37.7秒後までにさらに19往復の維持通信を記録しました。CY4500 の Extended Control 164件はすべて KeepAlive または KeepAlive_ACK でした。別の EPR OFFER 試験では `CAPTURE WARN`、`SPR:000`、`E0:001`、`E1:001` で、表示した4件の PDO が CY4500 と一致しました。CY4500 には冒頭の SPR の提示もあり、両試験の VBUS は約5 Vでした。

その次の実機試験では `PD_RX_PRIORITY` 版を使用したと報告がありました。EPR OFFER の写真2枚はいずれも `CAPTURE WARN`、`RX:019`、`E0:001`、`E1:001` で、表示された 28 V / 5 A、36 V / 5 A、48 V / 5 A、15～48 V AVS / 240 W はそれぞれの CY4500 記録と一致しました。SNIFF の2回はどちらも `DONE WARN` で、1回目は17件・GoodCRC 15件、2回目は16件・GoodCRC 16件です。対応する CY4500 の各記録は GoodCRC を除く SOP メッセージ18件と GoodCRC 18件でした。1回目の写真の2～17行には C00 と C01 が含まれ、電源からの `ACCEPT I07` がありません。1行目の写真はありません。2回目の16行はすべて写真にあり、電源からの `C01` と `ACCEPT I02` がありません。記録された VBUS は約5 Vで、48 V は提示能力です。この結果だけでは取りこぼしの原因は確定できません。

`PD_CC1_EPR_FAST` 版の EPR OFFER は、1回目が `NO EPR WARN`、`E0:001 E1:000` で終了し、別の1回は 28 V / 5 A、36 V / 5 A、48 V / 5 A、15～48 V AVS / 240 W を表示しました。対応する CY4500 の記録にはいずれも EPR 能力情報の両チャンクがあります。PD SNIFF の2回はそれぞれ11件で `FIFO FULL`、17件で `DONE WARN` でした。CY4500 の各記録には GoodCRC を除く SOP メッセージ18件と GoodCRC 18件があり、どちらの T3P 履歴にも電源が送った EPR 能力情報の後半チャンクがありません。1回目は通信途中で停止し、2回目は残りのメッセージが順に表示され、GoodCRC は18件でした。CY4500 が記録した VBUS は約5 Vで、提示された 48 V が実際に出力されたことを意味しません。後半チャンクを取りこぼした物理的な原因は未確定です。

それ以前の `PD_FIFO_WARN` 版では、EPR OFFER は `NO EPR WARN`、`RX:025`、`SPR:001`、`E0:000`、`E1:000` で終了しましたが、対応する CY4500 の記録には EPR 能力情報の両チャンクがありました。PD SNIFF の2回分はどちらも `DONE WARN` で、T3P は各16件・GoodCRC 17件、対応する CY4500 は各21件・GoodCRC 21件でした。両回とも T3P の一覧には EPR 能力情報の両チャンクがありません。CY4500 の SOP 記録から CRC が一致するパケットを再構成してホスト側のデコーダーへ入力すると、3回とも EPR 能力情報を組み立てられました。受信や処理間隔の調査に役立ちますが、取りこぼしの物理的な原因はまだ確定しません。

その後の `PD_SNIFF_FAST` 版では、2回の PD SNIFF がともに `FIFO ALERT` で停止し、各7件・GoodCRC 7件でした。表示された7件はそれぞれ CY4500 記録の冒頭と一致します。別の EPR OFFER 試験も `FIFO ALERT` で停止し、`RX:002`、`SPR:001`、EPR チャンクは0件でした。CY4500 の3つの記録にはいずれも CRC 一致の SOP メッセージ18件と SOP GoodCRC 18件があり、EPR の両チャンクも含まれます。ファームは保持されたアラートと RX 満杯の状態を区別せず停止していたため、写真だけではどちらが原因か、実際にデータが失われたかは判断できません。後続のファームでは保持アラートだけでは停止せず警告を残し、上記の新しい試験でその動作を確認しました。

その後、従来の受信処理を使い CY4500-EPR を直列に入れて2回比較しました。CY4500 の各記録には、GoodCRC を除く CRC 一致の SOP メッセージが18件、SOP の GoodCRC が18件ありました。SOP′ のケーブル通信と接続イベントはこの件数に含めていません。1回目の T3P 写真はメッセージ12件・GoodCRC 10件で、通信の途中で6件を取りこぼしています。2回目は18件・18件で、表示されたメッセージの順番と読み取れる内容が CY4500 と一致しました。GoodCRC は件数が一致しますが、T3P の表示から個々のパケットを識別することはできません。両回の記録では EPR Request が固定 PDO 1 の 5 V・3 A を要求し、CY4500 の VBUS 記録は約5 Vでした。これは EPR 電圧での契約や実際の負荷電流を確認したものではありません。他の通信での受信漏れや、1回目の取りこぼしの原因は引き続き未確認です。

それ以前の PD SNIFF 試験では、T3P の写真に `DONE`、`CC1`、`MSG:010`、`GCRC:011` が表示されました。保持された8行には `ACCEPT`、`PS_RDY`、`EPR_MODE` があり、送信元のロール表示には `SRC` と `SNK` の両方が含まれていました。1つの接続条件で解析した履歴を表示できたことを示していますが、通信全体の受信、EPR_MODE の内容、契約成立や実際の VBUS 電圧はこの写真からは確認できません。

2026-11-07、ユーザーは当初、下流に負荷を接続しない場合に電源ケーブルの片向きで SPR を受信でき、シンク接続時はプロファイル画面で SPR を受信できなかったと報告しました。その時点では EPR も受信できていませんでした。T3P に給電した状態で電源側メスポートを抵抗モードで測ると CC1 が約 5.1 kΩ、CC2 が数 MΩ だったとのことです。通電中の抵抗測定だけで内部配線や原因は断定できません。

その後の診断比較では、シンクなしの写真は CC1 で `NO EPR CAPS`、`RX:122`、`SPR:122`、`E0:000`、`E1:000` を示しました。ユーザーが 5 V のシンク接続状態と報告した別の写真では、`CAPTURED`、`RX:019`、`SPR:001`、`E0:001`、`E1:001` を表示。固定 28 V / 5 A、36 V / 5 A、48 V / 5 A と AVS 15～48 V / 240 W PDP の提示を受信できました。5 V はユーザーの報告で、写真は電源の提示内容を示しており、実際の VBUS 電圧や EPR 契約成立を示すものではありません。この試験ではシンク接続中に SPR Source_Capabilities も1件数えていますが、別の SPR プロファイル画面は写っていません。

2026-11-06 にユーザーからデモを T3P へ書き込んだ報告と写真を受領しました。写真ではデモ画面、2種類の模擬値、積算値とグラフ履歴の変化を確認できます。報告された試験用に準備したデモ画像のローカル SHA-256 は `b557f5fd89697917a11d07538279e0a6b78f83325ad2aa23b85af3db35f4f0ef` です。写真だけではボタン、正確な時間、電源を入れ直した後の起動、実測は確認できていません。

その後、積算値のゼロ表示が見えなかったとの報告と動画を受け取りました。動画ではボタン操作後に Ah と Wh がともに `0.000` になり、その後ふたたび増えることを確認できます。動画から書き込まれた画像のリビジョンは特定できません。現在のコードは画面の分割描画中もボタンを確認し、リセット検出後に積算値を再取得して描画します。この変更自体の実機確認はこれからです。

Pi32v2 機械語を命令シミュレーターで実行し、デモ62秒分の画面を PC 版と比較した結果、57,600画素が一致しました。その他の入力は引き続き検証が必要です。

実験的な実測候補も追加しました。PB2/PB1 のソフト I²C で 0x40 の計測回路に接続し、純正ファームが使うチップ ID `0x2260` と連続変換モードを確認してから電圧・シャント電圧を読みます。画面には `UNCAL` と表示し、個体校正を適用しない名目換算値を示します。ID または通信に失敗すると `---` を表示します。極性、換算精度と時間の精度は未確認です。

2026-11-06、ユーザーがこの実測候補を実機で試しました。写真の表示値は測定ケーブルなしで 3.008 V、報告された CC2 の向きで 4.991 V、CC1 の向きで 1.839 V です。電源は AOHI AOC-C022 の C1 端子。電流はほぼ 0 A で、ときどき −0.001 A が表示されました。各状態での T3P の実際の VBUS はまだ独立に確認していません。向きによる差と未接続時の電圧を調査中で、基準器との校正も未実施です。

この電源の ASD-PD31 ログでは、最初の PDO 読み出しは空で、再試行後に 5 V / 3 A の固定 PDO などを取得しています。このログだけでは T3P 接続時の電圧は確定できません。純正アプリは同じ I²C バス上の 7-bit アドレス 0x22 にアクセスし、`fusb302_task` を持ちます。実チップの識別は未確認です。オプションの `--pd-diagnostic` ビルドでは 0x01、0x02、0x08、0x0B、0x40 レジスターを読み取ります。CC 設定、PD 要求、読み出しで消去される割込みレジスターの読取りは行いません。

その後、診断版の実機写真3枚ではすべて `PD 91 03 02 01 01` が表示され、電圧表示は測定ケーブルなしで 3.010 V、ケーブルの2つの向きで 4.994 V と 1.865 V でした。ID とレジスターの値は FUSB302B 互換の回路と整合しますが、チップの刻印などによる確認はまだです。FUSB302B と同じレジスター構成なら `SWITCHES0=03` は両 CC のプルダウンを有効にし、CC 測定対象を選択していません。したがって変化しない `STATUS0` から CC 電圧は判定できません。T3P の CC 配線は引き続き独立した確認が必要です。

さらに、電源ケーブルと T3P の間に USB-C ブレークアウト基板を挟んだ測定では、報告された CC2 の向きで 4.992 V、CC1 では約 1.733 V をテスターで観測し、どちらも T3P の表示と小数3桁まで一致しました。CC1 の電圧は接続直後は 2 V 超で、時間とともに低下しました。この2点では電圧表示を裏付けますが、全測定範囲での精度や低電圧の原因は確定しません。ブレークアウト基板を挟んでいるため、先のケーブル直結時と完全に同じ接続ではありません。

Framework FRANDVCH00 電源、住友電工 DC-010338 ケーブル、ASD-PD31 負荷での写真では、固定 5 V 要求時の電流表示は T3P / ASD-PD31 の順に 1.002 / 1.000 A、1.500 / 1.500 A、3.007 / 3.000 A でした。固定 20 V および 48 V 要求時はそれぞれ 5.011 / 5.000 A、5.012 / 5.000 A で、48 V の写真では T3P は 47.435 V、237.754 W を示しました。HID は PC に接続したアイソレーターから給電しました。これらは別機器との比較であり校正ではありません。電圧の測定位置も異なり得ます。PD 契約を要求したのは ASD-PD31 です。積算時間・電力量の精度は実機では未検証です。

約 1 A を約 8 分 35 秒流した試験では、積算表示は 0.143 Ah、0.724 Wh、写真の瞬間値は 1.001 A、5.069 W でした。およその経過時間・負荷と整合しますが、開始時刻、負荷の変動、画面更新と未校正の影響があるため精密な時計・積算精度の検証ではありません。グラフは微小なノイズで全画面に伸びないよう縦軸の最小上限を 1 W にしました。数値や積算処理は変更していません。

その後、グラフ変更版の実機写真で無負荷時の棒が下端に収まり、約 1 A の負荷時にグラフが立ち上がることを確認しました。負荷時の写真では T3P と ASD-PD31 ともに 1.000 A を示しています。

## PC プレビュー

Rust 2024 と Cargo を用意して実行します。

```sh
cargo test --offline
cargo run --offline --bin preview -- preview.ppm
```

240×240 の PPM 画像が生成されます。表示値は模擬データです。

## 実験的な実機向けビルド

Rust 1.99.1（`58cf8f9c3`）とその `thumbv7em-none-eabihf` 用 core、[Pi32v2 LLVM 23.1.5-2](https://github.com/jvitkauskas/pi32v2-llvm/releases/tag/pi32v2-23.1.5-2)、入口コード用の JieLi アセンブラーを使います。32-bit LLVM IR を Pi32v2 に変換する実験的な構成です。アクティブ時間モードではシミュレーター上で Wh がずれたため、デモには経過時間モードを使っています。

各自で T3P 純正 3.0.3 を入手し、[jl-misctools](https://github.com/kagaimiq/jl-misctools) で展開してください。`app.bin` の SHA-256 は `ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c` です。LCD 初期化列はそのファイルからローカルの `build/` に生成します。

```sh
python build.py --rustc RUSTCのパス --llvm PI32V2_LLVMのパス --jieli-clang JIELI_CLANGのパス --stock-app 展開したAPP_BINのパス
cargo run --offline --bin preview -- build/preview.ppm
python check_pi32.py --sim PI32V2_ISAのパス
```

実測候補は `build.py` に `--measurement` を付けます。PC 比較画像を `cargo run --offline --bin preview -- build/demo-graph.ppm --power`、`cargo run --offline --bin preview -- build/measurement/preview-measurement.ppm --measurement` と、同様に `preview-energy.ppm --measurement --energy`、`preview-power.ppm --measurement --power`、`preview-voltage.ppm --measurement --voltage`、`preview-current.ppm --measurement --current`、`preview-missing.ppm --measurement --missing`、`preview-long.ppm --measurement --long-history --power --window10` で生成し、`python check_pi32.py --build-dir build/measurement --measurement --sim PI32V2_ISAのパス` で照合します。

PD 診断版は `python build.py --pd-diagnostic` でビルドします。上記7枚の実測比較画像を `build/pd-diagnostic` に生成し、共有の `build/demo-graph.ppm` と、`cargo run --offline --bin preview -- build/pd-diagnostic/preview-pd.ppm --measurement --missing --pd` で診断表示を生成して、`python check_pi32.py --build-dir build/pd-diagnostic --measurement --pd-diagnostic --sim PI32V2_ISAのパス` で照合します。表示される5バイトは順に 0x01、0x02、0x08、0x0B、0x40 レジスターの値です。

PD PROFILES 試験版は `python build.py --pd-profiles` でビルドします。`build/pd-profiles` に上記の実測比較画像と共有画像2枚（`preview.ppm` と `demo-graph.ppm`）を生成し、それぞれプレビューコマンドに `--profiles-ui` を加えます。`preview-profiles-ready.ppm`、`preview-profiles-listening.ppm`、`preview-profiles-offer.ppm` は `--measurement` にそれぞれ `--profiles-page`、`--profiles-listening`、`--profiles-offer` を加えて生成します。EPR の比較画像 `preview-epr-ready.ppm`、`preview-epr-timeout.ppm`、`preview-epr-offer.ppm`、`preview-epr-partial.ppm`、`preview-epr-diag.ppm` は `--measurement` にそれぞれ `--epr-page`、`--epr-timeout`、`--epr-offer`、`--epr-partial`、`--epr-diag` を加えて生成します。`preview-sniff-ready.ppm`、`preview-sniff-log.ppm`、`preview-sniff-old.ppm`、`preview-sniff-alert.ppm` は `--measurement` にそれぞれ `--sniff-page`、`--sniff-log`、`--sniff-old`、`--sniff-alert` を加えて生成します。`preview-cable-ready.ppm`、`preview-cable-listening.ppm`、`preview-cable-summary.ppm`、`preview-cable-raw.ppm`、`preview-cable-flags.ppm`、`preview-cable-timeout.ppm` は `--measurement` にそれぞれ `--cable-page`、`--cable-listening`、`--cable-summary`、`--cable-raw`、`--cable-flags`、`--cable-timeout` を加えて生成します。`python check_pi32.py --build-dir build/pd-profiles --measurement --pd-profiles --sim PI32V2_ISAのパス` で照合します。

`python pack_demo.py --build-dir build/pd-profiles --output-name T3P_RUST_METER_PD_CABLE_3.0.3.bin` でローカルの更新ファイルを作ります。

ケーブル単体の能動読取版は `python build.py --pd-cable-read` でビルドします。同じ比較画像を `cargo run --offline --features pd-cable-read --bin preview` で `build/pd-cable-read` に生成します。`--measurement --profiles-ui` に `--cable-vbus` または `--cable-no-ra` を加え、それぞれ `preview-cable-vbus.ppm` と `preview-cable-no-ra.ppm` を追加します。同じ共通オプションに `--cable-scan=N` を加え、`N` を `0`・`6`・`8` として `preview-cable-scan-N.ppm` を作ります。この比較には実機の Identity 応答と合成した追加応答を使います。更新ファイルは `python pack_demo.py --build-dir build/pd-cable-read --output-name T3P_RUST_METER_PD_CABLE_READ_V3_3.0.3.bin` で作ります。

**CHARGER V5** は、まず `cargo build --bin preview --features pd-charger` で PC 版をビルドし、`python preview_refs.py --pd-charger` で比較画像を `build/pd-charger` にまとめて生成します。`python build.py --pd-charger` で本体をビルドし、`python pack_demo.py --build-dir build/pd-charger --output-name T3P_RUST_METER_PD_CHARGER_V5_3.0.3.bin` で更新ファイルを作ります。更新ファイルの作成前に Pi32v2 の描画と照会のリプレイを検証します。比較画像は、必ず対象の機能で PC 版をビルドしてから生成してください。

**SYSTEM V2** は `cargo test --offline --features system` と `python tests/check_system.py --cc PC用CLANGのパス` で検証します。`cargo build --offline --bin preview --features system` で PC 版をビルドし、`python preview_refs.py --system` で比較画像58枚を生成します。`python build.py --system` で本体をビルドし、`python pack_demo.py --build-dir build/system --output-name T3P_RUST_METER_SYSTEM_V2_3.0.3.bin` で更新ファイルを作ります。既存の CHARGER・ケーブル・PD に加え、SYSTEM の描画と操作をリプレイで検証します。[SYSTEM の検証データ](tests/README.md#system-diagnostics)は、実測値とは区別した作成データです。

**RD TEST** は実測版の7枚の比較画像を `build/pd-rd-test` に用意します。さらに `cargo run --offline --features pd-rd-test --bin preview -- PATH --rd-off` と `--rd-5v` で、それぞれ `preview-rd-off.ppm` と `preview-rd-5v.ppm` を同じ場所に作ります。`python pack_demo.py --build-dir build/pd-rd-test --output-name T3P_RUST_METER_PD_RD_TEST_V2_3.0.3.bin` で更新ファイルを作ります。

**PD ACK TEST** は実測版の7枚の比較画像を `build/pd-ack-test` に用意し、`--features pd-ack-test` で Rd 画面の比較画像2枚を生成します。`python pack_demo.py --build-dir build/pd-ack-test --output-name T3P_RUST_METER_PD_ACK_TEST_V2_3.0.3.bin` で更新ファイルを作ります。

シミュレーター検査には固定版の [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa/tree/66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f) が必要です。ビルド結果の ELF はオフライン検証用です。`pack_demo.py` は現時点で別のローカル `doom_on_t3p` 調査環境、展開済み純正ファーム、梱包ツールにも依存します。純正ファームと更新バイナリは本リポジトリでは配布していません。

## ハードウェア調査

純正 3.0.3 の静的解析では、PB2/PB1 のソフト I²C、アドレス 0x40、レジスター 0x02 と 0x01 の読み取りを確認しています。INA226 と似たレジスター構成ですが、チップ ID、実配線、校正、実際の測定値は今後確認します。`check_stock.py` で展開済み純正アプリと記録したバイト列を照合できます。

## 出典とライセンス

`vendor/kws-x1/energy.rs` と `power_history.rs` は [KWS-X1 Standalone のコミット 6942fc6](https://github.com/kralonur/kws-x1-standalone-rs/tree/6942fc6052ac38e7afbd7d31a22ee8789f55c29a/firmware-core/src) に由来し、デュアルライセンスの MIT を選択しています。静的初期化のためコンストラクターを変更しています。元の MIT 表示は `vendor/kws-x1/LICENSE-MIT` にあります。

T3P の起動・基板コードはローカルの DOOM on T3P 調査プロジェクトに由来します。本プロジェクトのコードは GPL-2.0-or-later です。[COPYING](COPYING) と [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。本プロジェクトは非公式で、ATORCH や KWS-X1 の作者とは提携していません。
