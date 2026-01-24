# HLAC-LDA Classification System for RA8E1

RA8E1マイコンとPC(MATLAB)を使用したHLAC特徴量抽出とLDA分類システムの実装ガイド

## 概要

このシステムは，以下の構成で画像分類を行います：

1. **PC(MATLAB)側**：
   - RA8E1からUDP経由で画像を受信・保存
   - HLAC特徴量の抽出
   - LDA分類器の学習(W，bパラメータの生成)

2. **RA8E1側**：
   - 画像キャプチャ
   - HLAC特徴量の抽出
   - LDA分類器による推論(PC側で学習したW，bを使用)

## システム構成

```
PC (MATLAB)                          RA8E1
┌─────────────────────┐             ┌─────────────────────┐
│ 1. 画像キャプチャ    │◄────UDP────┤ カメラ画像送信       │
│    (hlac_image_     │             │                     │
│     capture.m)      │             │                     │
├─────────────────────┤             ├─────────────────────┤
│ 2. HLAC特徴抽出     │             │ HLAC特徴抽出        │
│    (extract_hlac_   │             │ (hlac_extract.h)    │
│     features.m)     │             │                     │
├─────────────────────┤             ├─────────────────────┤
│ 3. LDA学習          │             │ LDA推論             │
│    (train_lda_      │─────W,b────►│ (lda_inference.h)   │
│     classifier.m)   │             │                     │
└─────────────────────┘             └─────────────────────┘
```

## セットアップ手順

### ステップ1: 画像データの収集

1. RA8E1を起動し，UDP経由で画像を送信する状態にする

2. MATLABで画像キャプチャツールを起動：
```matlab
>> cd matlab
>> hlac_image_capture
```

3. 操作方法：
   - 数字キー `0-9`：対応するクラスラベルで画像を保存
   - `s`：統計情報を表示
   - `q`：終了

4. 各クラスごとに十分な枚数(最低20枚以上推奨)の画像を収集

保存先ディレクトリ構成：
```
hlac_training_data/
├── class0/
│   ├── class0_20260117_101520_123.png
│   ├── class0_20260117_101522_456.png
│   └── ...
├── class1/
│   └── ...
└── ...
```

### ステップ2: HLAC特徴量の抽出

MATLABで特徴量を抽出：

```matlab
>> cd matlab

% クラス名定義
>> class_names = {'class0', 'class1', 'class2', 'class3', 'class4'};

% データディレクトリ
>> data_dir = 'hlac_training_data';

% HLAC特徴量抽出(2次，25次元)
>> features_table = extract_hlac_from_dataset(data_dir, class_names, 2);

% 特徴量の可視化(PCA)
>> visualize_hlac_features(features_table, class_names);

% 保存
>> save('hlac_features.mat', 'features_table', 'class_names');
```

### ステップ3: LDA分類器の学習

MATLABでLDA分類器を学習：

```matlab
% 特徴量データ読み込み(既に抽出済みの場合)
>> load('hlac_features.mat');

% LDA学習とパラメータ出力
>> [lda_model, W, b] = train_lda_classifier(features_table, class_names, 'lda_model');

% 推論テスト
>> test_lda_inference(lda_model, features_table, class_names);
```

生成されるファイル：
```
lda_model/
├── lda_model.mat           # MATLAB形式のモデル
├── lda_weights_W.csv       # 重み行列(CSV)
├── lda_bias_b.csv          # バイアスベクトル(CSV)
├── lda_params.h            # C言語用ヘッダファイル ★重要★
├── lda_params_info.txt     # パラメータ情報
└── confusion_matrix.png    # 混同行列
```

### ステップ4: RA8E1への実装

1. **lda_params.hをRA8E1プロジェクトにコピー**
```
lda_model/lda_params.h → RA8E1_prj/src/lda_params.h
```

2. **推論コードの実装例**

```c
#include "hlac_extract.h"
#include "lda_inference.h"
#include "lda_params.h"

void process_camera_frame(uint8_t *frame_buffer, uint16_t width, uint16_t height)
{
    // 1. 画像構造体の準備
    grayscale_image_t img = {
        .data = frame_buffer,
        .width = width,
        .height = height
    };
    
    // 2. HLAC特徴量抽出
    float features[HLAC_NUM_FEATURES];
    hlac_extract_features(&img, features);
    
    // 3. LDA推論
    float scores[LDA_NUM_CLASSES];
    int predicted_class = lda_predict(features, scores);
    
    // 4. 結果の表示・送信
    printf("Predicted class: %d (%s)\n", 
           predicted_class, 
           lda_get_class_name(predicted_class));
}
```

3. **RGB画像の場合**
```c
void process_rgb_camera_frame(uint8_t *rgb_frame, uint16_t width, uint16_t height)
{
    // グレースケール変換用バッファ
    uint8_t gray_frame[width * height];
    
    // RGB→グレースケール変換
    rgb_to_grayscale(rgb_frame, gray_frame, width, height);
    
    // グレースケール画像を処理
    process_camera_frame(gray_frame, width, height);
}
```

## ファイル構成

### MATLAB側(学習用)
```
matlab/
├── hlac_image_capture.m        # 画像キャプチャツール
├── extract_hlac_features.m     # HLAC特徴量抽出
├── train_lda_classifier.m      # LDA学習
├── udp_photo_receiver.m        # リアルタイム動画表示(既存)
└── viewQVGA_YUV.m             # YUV表示ユーティリティ(既存)
```

### RA8E1側(推論用)
```
src/
├── hlac_extract.h              # HLAC特徴量抽出
├── lda_inference.h             # LDA推論
├── lda_params.h                # 学習済みパラメータ(MATLABで生成)
└── hlac_lda_example.c          # 使用例
```

## HLAC特徴量について

### 1次HLAC(5次元)
- パターン: 中心ピクセルと隣接ピクセルの1次自己相関
- 特徴数: 5
- 計算コスト: 低

### 2次HLAC(25次元)
- パターン: 3×3近傍の2次自己相関
- 特徴数: 25
- 計算コスト: 中
- **推奨**: より高い識別性能

## パフォーマンス

### メモリ使用量(2次HLAC，5クラスの場合)
- HLAC特徴量: 25 × 4 bytes = 100 bytes
- LDAスコア: 5 × 4 bytes = 20 bytes
- 合計: ~120 bytes

### 処理時間の目安(QVGA 320×240)
- HLAC特徴抽出: ~10-50ms(実装による)
- LDA推論: ~1ms未満
- 合計: ~10-50ms

## トラブルシューティング

### 分類精度が低い場合
1. **データ収集を見直す**
   - 各クラス30枚以上のサンプルを収集
   - クラス間のサンプル数を均等にする
   - 照明条件や撮影角度のバリエーションを増やす

2. **特徴量の確認**
   - `visualize_hlac_features()`で特徴空間の分布を確認
   - クラス間の分離が不十分な場合は，異なる特徴量を検討

3. **HLAC次数の変更**
   - 2次HLAC(25次元)を使用(推奨)
   - より複雑なパターン認識が可能

### UDP通信エラー
1. ファイアウォール設定を確認
2. ポート番号(9000)が使用可能か確認
3. RA8E1のIPアドレス設定を確認

### RA8E1での推論エラー
1. `lda_params.h`が正しくコピーされているか確認
2. HLAC_ORDERの設定がMATLAB側と一致しているか確認
3. 画像サイズが正しく設定されているか確認

## 応用例

### リアルタイム分類
```c
void realtime_classification_loop(void)
{
    while (1) {
        // カメラフレーム取得
        capture_camera_frame();
        
        // HLAC+LDA分類
        int class_id = classify_camera_frame();
        
        // 結果に基づいたアクション
        take_action_based_on_class(class_id);
        
        // フレームレート調整
        delay_ms(100);  // 10 FPS
    }
}
```

### UDP経由での結果送信
```c
void send_classification_result(int class_id, float *scores)
{
    char result_str[128];
    snprintf(result_str, sizeof(result_str), 
             "CLASS:%d NAME:%s SCORE:%.2f",
             class_id, 
             lda_get_class_name(class_id),
             scores[class_id]);
    
    udp_send(result_str, strlen(result_str));
}
```

## 参考文献

1. HLAC (Higher-order Local Auto-Correlation)
   - Otsu, N., & Kurita, T. (1998). A new scheme for practical flexible and intelligent vision systems.

2. LDA (Linear Discriminant Analysis)
   - Fisher, R. A. (1936). The use of multiple measurements in taxonomic problems.

## ライセンス

このコードはMITライセンスの下で提供されています．

## 更新履歴

- 2026-01-17: 初版作成
  - HLAC特徴量抽出の実装
  - LDA分類器の学習・推論の実装
  - RA8E1用C言語コードの実装
