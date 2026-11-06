# T3P Standalone

[English](README.md)

ATORCH T3P USB 電力計向けの実験的な Rust ファームウェアです。[KWS-X1 Standalone](https://github.com/kralonur/kws-x1-standalone-rs) と、ローカルの DOOM on T3P 基板調査をもとに開発しています。

## 現在の状態

現在のビルドは、**模擬データを使った起動・表示デモ**です。電圧、符号付き電流・電力、Ah・Wh と直近60秒の電力グラフを表示します。PA11 ボタンで積算値をリセットする処理も実装しています。実測、USB PD、充電プロトコルの実装はこれからです。

2026-11-06 にユーザーからデモを T3P へ書き込んだ報告と写真を受領しました。写真ではデモ画面、2種類の模擬値、積算値とグラフ履歴の変化を確認できます。報告された試験用に準備したデモ画像のローカル SHA-256 は `b557f5fd89697917a11d07538279e0a6b78f83325ad2aa23b85af3db35f4f0ef` です。写真だけではボタン、正確な時間、電源を入れ直した後の起動、実測は確認できていません。

その後、左ボタンを押しても積算値のゼロ表示が見えなかったとの報告を受けました。現在のコードは画面の分割描画中もボタンを確認し、リセット検出後に積算値を再取得して描画します。PC 側で回帰検査済みで、実機確認はこれからです。

Pi32v2 機械語を命令シミュレーターで実行し、デモ62秒分の画面を PC 版と比較した結果、57,600画素が一致しました。その他の入力は引き続き検証が必要です。

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

シミュレーター検査には固定版の [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa/tree/66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f) が必要です。ビルド結果の ELF はオフライン検証用です。`pack_demo.py` は現時点で別のローカル `doom_on_t3p` 調査環境、展開済み純正ファーム、梱包ツールにも依存します。純正ファームと更新バイナリは本リポジトリでは配布していません。

## ハードウェア調査

純正 3.0.3 の静的解析では、PB2/PB1 のソフト I²C、アドレス 0x40、レジスター 0x02 と 0x01 の読み取りを確認しています。INA226 と似たレジスター構成ですが、チップ ID、実配線、校正、実際の測定値は今後確認します。`check_stock.py` で展開済み純正アプリと記録したバイト列を照合できます。

## 出典とライセンス

`vendor/kws-x1/energy.rs` と `power_history.rs` は [KWS-X1 Standalone のコミット 6942fc6](https://github.com/kralonur/kws-x1-standalone-rs/tree/6942fc6052ac38e7afbd7d31a22ee8789f55c29a/firmware-core/src) に由来し、デュアルライセンスの MIT を選択しています。静的初期化のためコンストラクターを変更しています。元の MIT 表示は `vendor/kws-x1/LICENSE-MIT` にあります。

T3P の起動・基板コードはローカルの DOOM on T3P 調査プロジェクトに由来します。本プロジェクトのコードは GPL-2.0-or-later です。[COPYING](COPYING) と [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。本プロジェクトは非公式で、ATORCH や KWS-X1 の作者とは提携していません。
