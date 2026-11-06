# T3P Standalone

[English](README.md)

ATORCH T3P USB 電力計向けの実験的な Rust ファームウェアです。[KWS-X1 Standalone](https://github.com/kralonur/kws-x1-standalone-rs) と、ローカルの DOOM on T3P 基板調査をもとに開発しています。

## 現在の状態

標準ビルドは、**模擬データを使った起動・表示デモ**です。別途、個体校正なしの実測ビルドもあります。正方形の画面を使い、KWS-X1 に寄せた **OVERVIEW** に電圧・符号付き電流・電力と最大値、**ENERGY** に Ah・Wh・経過時間、**GRAPH** に直近1・2・5・10分の電力・電圧・電流を表示します。別ビルドでは電源が提示した SPR と EPR の PDO を受信・表示できます。

ENERGY 画面の時間は起動時または積算リセット後の経過時間です。測定値が取得できない間や電流ゼロの間も進みます。旧画面の実機で時間が進み Ah・Wh とともにリセットされることを確認しました。表示は `99:59:59` で止まり（実機未確認）、積算値と時間は再起動後に復元されません。PD 診断版では OVERVIEW の PROTOCOL 欄に生の診断値を表示します。

KWS 風の画面、ダブル押し、長い期間のグラフを実機で確認しました。最大値は電源断で消えます。左ボタンの短押し・離す操作で、OVERVIEW、ENERGY、電力・電圧・電流の各グラフを順に切り替えます。グラフ画面ではダブル押しで1・2・5・10分を切り替えます。OVERVIEW と ENERGY ではダブル押しも1ページ進みます。約1秒の長押しで Ah・Wh・経過時間と最大値をリセットします。短押しはダブル押し判定のため約350 ms 後に確定します。電流グラフは絶対値を表示します。グラフ履歴は画面間で共有し、リセットしても消えません。各最大値は異なる時点の値になり得るため、最大電圧と最大電流の積が最大電力と一致するとは限りません。

正方形向けの目盛り付き折れ線グラフは実機確認済みです。各期間を60点にまとめ、それぞれ1・2・5・10秒の平均を表示します。一部に欠測がある区間は孤立した点、全て欠測の区間は空白にします。縦軸はゼロと、単位付きの上限を表示します。上限は表示範囲の平均値の最大に合わせて 1・2・2.5・5 の段階に切り上げ、最小 1 W／V／A とします。上限が変わると線全体の高さも変わります。

## PD PROFILES 試験版

`--pd-profiles` でビルドすると、電流グラフの次に **PD PROFILES** が出ます。左ボタンのダブル押しで CC1 の受信を10秒間待ち、5 V の試験構成で電源をつなぎ直します。HID から別給電すると、測定側の電源をつなぐ前に受信を開始できます。同じ画面でダブル押しするたびに受信対象の CC を切り替えます。最初に CRC が一致した SPR の Source_Capabilities を表示し、再取得または電源断まで RAM に保持します。SPR の5件の PDO は T3P 実機で受信・表示できました。

次の **EPR OFFER** ではダブル押しで30秒間受信を待ちます。5 V から始め、接続した ASD-PD31 などの PD シンクから EPR 情報を問い合わせるか EPR モードに入ります。画面を切り替えた直後の取得では直前に選択した CC を使い、同じ画面で再度ダブル押しすると CC が切り替わります。EPR_Source_Capabilities は通常2つのチャンクに分かれます。両方の CRC が一致し順に受信できたら `CAPTURED`、チャンク1だけを受信したときはその EPR PDO を `PARTIAL` と表示します。`NO EPR CAPS` は待機時間中に完全な情報を捕捉できなかったことを示します。

固定 PDO は電圧・最大電流・計算した電力、PPS は電圧範囲と電流上限を表示します。SPR AVS は 9～15 V と 15～20 V の電流上限、EPR AVS は電圧範囲と PDP を表示します。それ以外の PDO は生の16進数で表示します。一覧の値は電源が提示した値です。起動時の CC 終端を保ち、PD 送信と自動 GoodCRC を無効のまま受信します。EPR 情報の取得には別のシンクからの問い合わせが必要で、EPR の受信とタイミングは実機確認待ちです。 [FUSB302B データシート](https://www.onsemi.com/pdf/datasheet/fusb302b-d.pdf) · [USB Power Delivery 仕様](https://www.usb.org/document-library/usb-power-delivery)

## 実機での検証

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

PD PROFILES 試験版は `python build.py --pd-profiles` でビルドします。`build/pd-profiles` に上記の実測比較画像と共有画像2枚（`preview.ppm` と `demo-graph.ppm`）を生成し、それぞれプレビューコマンドに `--profiles-ui` を加えます。`preview-profiles-ready.ppm` は `--measurement --profiles-page`、`preview-profiles-offer.ppm` は `--measurement --profiles-offer` で生成します。EPR の比較画像 `preview-epr-ready.ppm`、`preview-epr-offer.ppm`、`preview-epr-partial.ppm` は `--measurement` にそれぞれ `--epr-page`、`--epr-offer`、`--epr-partial` を加えて生成します。`python check_pi32.py --build-dir build/pd-profiles --measurement --pd-profiles --sim PI32V2_ISAのパス` で照合します。

`python pack_demo.py --build-dir build/pd-profiles --output-name T3P_RUST_METER_EPR_3.0.3.bin` でローカルの更新ファイルを作ります。

シミュレーター検査には固定版の [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa/tree/66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f) が必要です。ビルド結果の ELF はオフライン検証用です。`pack_demo.py` は現時点で別のローカル `doom_on_t3p` 調査環境、展開済み純正ファーム、梱包ツールにも依存します。純正ファームと更新バイナリは本リポジトリでは配布していません。

## ハードウェア調査

純正 3.0.3 の静的解析では、PB2/PB1 のソフト I²C、アドレス 0x40、レジスター 0x02 と 0x01 の読み取りを確認しています。INA226 と似たレジスター構成ですが、チップ ID、実配線、校正、実際の測定値は今後確認します。`check_stock.py` で展開済み純正アプリと記録したバイト列を照合できます。

## 出典とライセンス

`vendor/kws-x1/energy.rs` と `power_history.rs` は [KWS-X1 Standalone のコミット 6942fc6](https://github.com/kralonur/kws-x1-standalone-rs/tree/6942fc6052ac38e7afbd7d31a22ee8789f55c29a/firmware-core/src) に由来し、デュアルライセンスの MIT を選択しています。静的初期化のためコンストラクターを変更しています。元の MIT 表示は `vendor/kws-x1/LICENSE-MIT` にあります。

T3P の起動・基板コードはローカルの DOOM on T3P 調査プロジェクトに由来します。本プロジェクトのコードは GPL-2.0-or-later です。[COPYING](COPYING) と [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。本プロジェクトは非公式で、ATORCH や KWS-X1 の作者とは提携していません。
