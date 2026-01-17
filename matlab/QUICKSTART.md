# Sobel + HLAC + LDA（MATLAB）クイックスタート

このフォルダ（matlab/）のスクリプトで、学習 → オンライン推論まで完結します。

パイプライン：

```
RA8E1 depth(8bit, 320x240) → Sobel(|P|+|Q|) → HLAC(order=2, 45次元) → LDA
```

## 1) 前提（Toolbox）

- オンライン推論（UDP受信）: DSP System Toolbox（`dsp.UDPReceiver`）
- 画像読み込み/配列処理: MATLAB標準
- 学習（LDA）: Statistics and Machine Learning Toolbox **無しでもOK**（自前LDAにフォールバックします）

## 2) 画像収集（学習データ作成）

```matlab
>> cd matlab
>> hlac_image_capture
```

- RA8E1からUDPでフレームが来ていることを確認
- 数字キー `0`, `1`, ... でクラスを指定して保存（保存先は `../hlac_training_data/class0` など）
- 欠損チャンクが多いフレームは `../hlac_training_data/_rejected/` に退避されます
- `q` で終了

目安：各クラス 20〜30枚以上（まずは少なくても動作確認できます）。

## 3) 学習（特徴抽出 → LDA）

一番簡単なのはワークフローを実行する方法です。

```matlab
>> cd matlab
>> hlac_lda_workflow
```

- Step3: 特徴抽出（初回は `y` 推奨。Enterだけでも `y` 扱いになります）
- Step4: LDA学習（Enterだけでも `y` 扱いになります）

生成物（リポジトリ直下）:

- `../hlac_features.mat`（中間成果物：特徴量テーブル）
- `../lda_model/lda_model.mat`（学習済みモデル：`W,b`）
- `../lda_model/lda_weights_W.csv`, `../lda_model/lda_bias_b.csv`

### 最小コード（手動でやる場合）

```matlab
cd matlab

class_names = {'class0','class1'};  % 使うクラスに合わせて変更
features_table = extract_hlac_from_dataset('../hlac_training_data', class_names, 2, true);
[lda_model, W, b] = train_lda_classifier(features_table, class_names, '../lda_model');
```

## 4) オンライン推論（UDP受信→判別）

学習済みモデルが `../lda_model/` にある状態で実行します。

```matlab
>> cd matlab
>> hlac_udp_inference
```

オプション例：

```matlab
% ポート変更
hlac_udp_inference('udp_port', 9000);

% 欠損が多いフレームは推論しない（デフォルト）
hlac_udp_inference('max_missing_chunks', 5);

% 欠損が多くても推論する
hlac_udp_inference('infer_on_rejected', true);
```

- 画面タイトルに `frame=... missing=... pred=...` が出ます
- 終了はウィンドウ上で `q`

## 5) つまずきポイント

### `LDAモデルが見つかりません` と出る

- `../lda_model/lda_model.mat` が無い状態です
- 先に `hlac_lda_workflow` の Step4 まで実行してモデルを作ってください

### `pca requires Statistics and Machine Learning Toolbox` と出る

- 可視化だけの問題です
- 本リポジトリでは可視化はスキップしても学習・推論は進みます

### UDPが受信できない

- Windowsファイアウォール
- RA8E1の送信先IP/ポート
- 既に同じポートを別アプリが使用していないか

関連ファイル：

- 学習ワークフロー: [hlac_lda_workflow.m](hlac_lda_workflow.m)
- 学習（LDA）: [train_lda_classifier.m](train_lda_classifier.m)
- オンライン推論: [hlac_udp_inference.m](hlac_udp_inference.m)
- モデル読み込み: [load_lda_params.m](load_lda_params.m)
