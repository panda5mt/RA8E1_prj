# Sobel + HLAC(25) + LDA：学習→推論（MATLABのみ）手順

このプロジェクトのPC側（MATLAB）で、次の流れを実行するための手順です。

- **学習データ取得**: RA8E1 → UDP(9000) → PCで画像保存
- **学習**: 画像 → Sobel(|P|+|Q|) → HLAC(25) → LDA学習 → `W,b` を出力
- **オンライン推論**: RA8E1 → UDP(9000) → PCで受信 → Sobel→HLAC→`W,b`で判別

> 方針: **C側は触らず**、MATLAB側のみで完結します。

---

## 前提

- MATLAB
- DSP System Toolbox（`dsp.UDPReceiver` を使用）
- RA8E1がPCに向けてUDPで画像フレームを送信していること
  - 受信データは **8bit depth画像 (320×240)** をチャンク分割して送っている想定です

---

## 使うファイル（入口）

- データ取得（保存）: [matlab/hlac_image_capture.m](../matlab/hlac_image_capture.m)
- 学習ワークフロー（対話式）: [matlab/hlac_lda_workflow.m](../matlab/hlac_lda_workflow.m)
- オンライン推論（UDP受信→判別）: [matlab/hlac_udp_inference.m](../matlab/hlac_udp_inference.m)

補助:
- 特徴抽出（Sobel+HLAC）: [matlab/extract_hlac_features.m](../matlab/extract_hlac_features.m)
- データセット一括抽出: [matlab/extract_hlac_from_dataset.m](../matlab/extract_hlac_from_dataset.m)
- LDA学習（W,b出力）: [matlab/train_lda_classifier.m](../matlab/train_lda_classifier.m)

---

## 出力フォルダ構成

データ取得後（学習に使う画像）:

- `hlac_training_data/class0/*.png`
- `hlac_training_data/class1/*.png`
- ...

欠損が多いフレーム（学習に混ぜない退避先）:

- `hlac_training_data/_rejected/class0/*.png`
- `hlac_training_data/_rejected/class1/*.png`
- ...

学習後（モデル出力）:

- `lda_model/lda_model.mat`（MATLAB用、W,b,class_names入り）
- `lda_model/lda_weights_W.csv`
- `lda_model/lda_bias_b.csv`
- `lda_model/lda_params.h`（将来C側へ持っていく用。現時点ではMATLAB推論に不要）

---

## 1) 学習データ取得（RA8E1→PCへ保存）

MATLABで:

```matlab
cd matlab
hlac_image_capture
```

操作:

- `0`〜`9`: 対応するクラスへ保存（例: `0`→`class0`）
- `s`: 保存枚数の統計表示
- `q`: 終了

保存ルール:

- 欠損チャンクが少ないフレームだけ `hlac_training_data/classX/` に保存
- 欠損が多いフレームは `hlac_training_data/_rejected/classX/` に退避（学習に混ぜない）
- ファイル名に `frame_id` と `missing`（欠損数）が入ります

---

## 2) 学習（Sobel→HLAC(25)→LDA→W,b出力）

MATLABで:

```matlab
cd matlab
hlac_lda_workflow
```

重要ポイント:

- [matlab/hlac_lda_workflow.m](../matlab/hlac_lda_workflow.m) の `config.class_names` を、**実際に使うクラスだけ**に合わせてください。
  - 例: `{'class0','class1','class2'}`
- ワークフロー内で以下を順に実行します
  - 画像枚数チェック
  - 特徴抽出（Sobel+HLAC）
  - LDA学習
  - 推論テスト
  - `lda_model/` へ保存

---

## 3) オンライン推論（RA8E1→UDP受信→判別）

学習済みモデルが `lda_model/lda_model.mat` にある状態で実行します。

MATLABで:

```matlab
cd matlab
hlac_udp_inference
```

動作:

- UDP(9000) で受信したフレームを復元して表示
- 同時に以下を計算して判別
  - Sobel（|P|+|Q|）
  - HLAC 2次（25次元）
  - `scores = W' * x + b`
  - `argmax(scores)` を予測クラス
- 画面タイトルに `frame_id / 欠損数 / 予測` を表示

終了:

- ウィンドウ上で `q`

設定変更（例）:

```matlab
% 欠損が多いフレームは推論スキップ（デフォルト missing<=5）
hlac_udp_inference('max_missing_chunks', 2)

% 欠損が多くても推論する（学習には推奨しないが、動作確認用）
hlac_udp_inference('infer_on_rejected', true)
```

---

## よくある詰まりポイント

- **画像が表示されない**
  - RA8E1がPCのIPへ送っているか
  - PC側の受信ポートが `9000` か
  - ファイアウォールでUDPがブロックされていないか

- **学習が進まない / クラスが足りない**
  - `hlac_training_data/classX/` に十分な枚数があるか
  - `config.class_names` が実際のクラス数と一致しているか

- **オンライン推論が動かない（モデルが無い）**
  - 先に学習を実行し、`lda_model/lda_model.mat` を作ってください

---

## 目安コマンド（最短）

```matlab
cd matlab
hlac_image_capture          % 1) データ取得
hlac_lda_workflow           % 2) 学習（W,b出力）
hlac_udp_inference          % 3) オンライン推論
```
