# Sobel + HLAC + LDA 画像分類システム

## 概要

このシステムは、Sobelエッジ検出、HLAC特徴量抽出、LDA分類を組み合わせた画像分類システムです。

### 処理の流れ

```
入力画像
   ↓
グレースケール変換
   ↓
Sobelフィルタ (|P| + |Q|)
   ↓
HLAC特徴量抽出 (25次元)
   ↓
LDA分類器
   ↓
クラス予測
```

### Sobelフィルタとは

Sobelフィルタは、画像のエッジ（輪郭）を検出するフィルタです。

- **水平Sobel (P)**: 縦方向のエッジを検出
  ```
  [-1  0  1]
  [-2  0  2]
  [-1  0  1]
  ```

- **垂直Sobel (Q)**: 横方向のエッジを検出
  ```
  [-1 -2 -1]
  [ 0  0  0]
  [ 1  2  1]
  ```

- **エッジ強度**: |P| + |Q| を計算

### なぜSobelフィルタを使うのか

1. **エッジ情報の強調**: 物体の輪郭や形状が明確になる
2. **照明変動に強い**: 明るさの変化に対してロバスト
3. **ノイズ除去効果**: 局所的な畳み込みでノイズを低減
4. **計算コストが低い**: 3×3カーネルで高速処理

## クイックスタート

### 1. 画像収集（2分）

```matlab
>> cd matlab
>> hlac_image_capture
```

キーボード操作:
- `0`-`9`: クラスラベルを付けて保存
- `s`: 統計表示
- `q`: 終了

各クラス20枚以上を推奨。

### 2. Sobel + HLAC + LDA学習（3分）

```matlab
>> hlac_lda_workflow
```

全ての質問に `y` と答えて実行。

### 3. 結果の確認

生成されるファイル:
```
lda_model/
├── lda_params.h           # RA8E1用パラメータ
├── lda_weights_W.csv      # 重み行列
├── lda_bias_b.csv         # バイアス
└── confusion_matrix.png   # 混同行列
```

## 詳細な使い方

### Sobel処理の可視化

単一画像でSobel処理を確認:

```matlab
>> img_path = 'hlac_training_data/class0/sample.png';
>> visualize_sobel_hlac_process(img_path);
```

表示内容:
1. 元画像
2. グレースケール画像
3. Sobel処理画像（|P|+|Q|）
4. 水平エッジ（|P|）
5. 垂直エッジ（|Q|）
6. HLAC特徴量の比較（Sobelあり・なし）

### Sobel効果の統計分析

データセット全体でSobel効果を分析:

```matlab
>> class_names = {'class0', 'class1', 'class2'};
>> plot_sobel_effect_on_features('hlac_training_data', class_names);
```

これにより、以下が表示されます:
- Sobelあり・なしの特徴空間分布（PCA）
- 統計情報（平均、標準偏差、範囲）

### 完全な使用例

全ステップを含む例:

```matlab
>> sobel_hlac_lda_example
```

このスクリプトは以下を実行します:
1. 単一画像でのテスト
2. データセット全体の特徴量抽出
3. Sobel効果の統計分析
4. LDA学習
5. 推論テスト
6. 新しい画像での推論例

## MATLAB関数リファレンス

### extract_hlac_features

Sobel + HLAC特徴量を抽出。

```matlab
features = extract_hlac_features(img, order, use_sobel)
```

**パラメータ:**
- `img`: 入力画像（RGB or グレースケール）
- `order`: HLAC次数（1 or 2、デフォルト=2）
- `use_sobel`: Sobelを使用するか（デフォルト=true）

**戻り値:**
- `features`: 特徴ベクトル（order=2なら25次元）

**例:**
```matlab
img = imread('sample.png');
features = extract_hlac_features(img, 2, true);  % Sobel+HLAC
```

### extract_hlac_from_dataset

データセット全体から特徴量抽出。

```matlab
features_table = extract_hlac_from_dataset(data_dir, class_names, order, use_sobel)
```

**パラメータ:**
- `data_dir`: データセットディレクトリ
- `class_names`: クラス名のセル配列
- `order`: HLAC次数（デフォルト=2）
- `use_sobel`: Sobelを使用するか（デフォルト=true）

**戻り値:**
- `features_table`: 特徴量とラベルを含むテーブル

**例:**
```matlab
class_names = {'apple', 'banana', 'orange'};
features_table = extract_hlac_from_dataset('data', class_names, 2, true);
```

### train_lda_classifier

LDA分類器を学習。

```matlab
[lda_model, W, b] = train_lda_classifier(features_table, class_names, output_dir)
```

**パラメータ:**
- `features_table`: 特徴量テーブル
- `class_names`: クラス名のセル配列
- `output_dir`: 出力ディレクトリ（デフォルト='lda_model'）

**戻り値:**
- `lda_model`: 学習済みLDAモデル
- `W`: 重み行列 [特徴次元 × クラス数]
- `b`: バイアスベクトル [クラス数]

### visualize_sobel_hlac_process

Sobel処理を可視化。

```matlab
visualize_sobel_hlac_process(img_path)
```

### compare_sobel_hlac_on_dataset

データセットでSobel効果を比較。

```matlab
compare_sobel_hlac_on_dataset(data_dir, class_names, num_samples)
```

### plot_sobel_effect_on_features

Sobel効果の統計分析。

```matlab
plot_sobel_effect_on_features(data_dir, class_names)
```

## パラメータ設定のガイドライン

### HLAC次数の選択

| 次数 | 特徴次元 | 計算コスト | 識別性能 | 推奨用途 |
|-----|---------|-----------|---------|---------|
| 1次 | 5次元   | 低        | 中      | 簡単なタスク |
| 2次 | 25次元  | 中        | 高      | 一般的なタスク（推奨） |

### Sobel使用の判断

| 状況 | Sobel使用 | 理由 |
|-----|----------|-----|
| エッジが重要 | ✓ 使用 | 形状・輪郭を強調 |
| テクスチャが重要 | ✗ 不使用 | 細かい模様を保持 |
| 照明変動が大きい | ✓ 使用 | 照明に対してロバスト |
| 色情報が重要 | ✗ 不使用 | グレースケール化で情報損失 |

### データ収集のガイドライン

1. **サンプル数**: 各クラス30枚以上推奨
2. **バランス**: 各クラスのサンプル数を均等に
3. **バリエーション**:
   - 異なる角度
   - 異なる照明条件
   - 異なる背景
4. **品質**: ピンボケや手ブレを避ける

## パフォーマンス

### 処理時間（QVGA 320×240）

| 処理 | 時間 |
|-----|------|
| Sobelフィルタ | ~1ms |
| HLAC抽出（2次） | ~5-20ms |
| LDA推論 | <1ms |
| **合計** | **~10-25ms** |

### メモリ使用量

| 項目 | サイズ |
|-----|-------|
| 入力画像（QVGA） | 76,800 bytes |
| Sobel画像 | 76,800 bytes |
| HLAC特徴量 | 100 bytes |
| LDAスコア | 20 bytes（5クラスの場合） |

## トラブルシューティング

### 分類精度が低い

**原因と対策:**

1. **データ不足**
   - 対策: 各クラス30枚以上収集

2. **クラス間の類似性が高い**
   - 対策: より識別しやすいクラスを選択
   - 対策: Sobel処理で特徴を強調

3. **照明条件が不統一**
   - 対策: Sobel処理を有効化（照明に強い）
   - 対策: 照明を統一して撮影

4. **データのばらつきが大きい**
   - 対策: 撮影条件を統一
   - 対策: データの前処理を追加

### Sobel処理後の画像が真っ白/真っ黒

**原因:**
- 画像のコントラストが低い
- エッジが少ない画像

**対策:**
```matlab
% コントラスト調整後にSobel適用
img_enhanced = imadjust(img);
features = extract_hlac_features(img_enhanced, 2, true);
```

### MATLAB関数が見つからない

**エラー:** `Undefined function or variable`

**対策:**
1. MATLABのカレントディレクトリを確認
```matlab
>> cd matlab
```

2. パスを追加
```matlab
>> addpath('matlab');
```

### UDP通信エラー

画像キャプチャツールで接続できない場合:

1. **ファイアウォール確認**
   - Windowsファイアウォールで許可

2. **ポート確認**
   - ポート9000が使用中でないか確認

3. **RA8E1のIP設定**
   - 正しいIPアドレスに送信しているか確認

## 応用例

### 1. リアルタイム分類

```matlab
% リアルタイムループ
while true
    % 画像取得（UDP受信など）
    img = get_latest_frame();
    
    % Sobel + HLAC + LDA
    features = extract_hlac_features(img, 2, true);
    pred_class = predict(lda_model, features');
    
    % 結果表示
    fprintf('予測: %s\n', class_names{pred_class+1});
    
    pause(0.1);  % 10 Hz
end
```

### 2. バッチ処理

```matlab
% 複数画像を一括処理
img_list = dir('test_images/*.png');

results = [];
for i = 1:length(img_list)
    img = imread(fullfile('test_images', img_list(i).name));
    features = extract_hlac_features(img, 2, true);
    pred = predict(lda_model, features');
    results = [results; pred];
end

% 結果の集計
fprintf('処理した画像数: %d\n', length(results));
```

### 3. クロスバリデーション

```matlab
% K-分割交差検証
k = 5;
cv = cvpartition(features_table.Label, 'KFold', k);

accuracies = zeros(k, 1);
for i = 1:k
    train_idx = training(cv, i);
    test_idx = test(cv, i);
    
    % 学習
    X_train = table2array(features_table(train_idx, 1:25));
    Y_train = features_table.Label(train_idx);
    mdl = fitcdiscr(X_train, Y_train);
    
    % テスト
    X_test = table2array(features_table(test_idx, 1:25));
    Y_test = features_table.Label(test_idx);
    Y_pred = predict(mdl, X_test);
    
    accuracies(i) = sum(Y_pred == Y_test) / length(Y_test);
end

fprintf('交差検証精度: %.2f%% ± %.2f%%\n', ...
        mean(accuracies)*100, std(accuracies)*100);
```

## 理論的背景

### Sobelフィルタの数学的定義

水平Sobel:
$$P = \begin{bmatrix} -1 & 0 & 1 \\ -2 & 0 & 2 \\ -1 & 0 & 1 \end{bmatrix} * I$$

垂直Sobel:
$$Q = \begin{bmatrix} -1 & -2 & -1 \\ 0 & 0 & 0 \\ 1 & 2 & 1 \end{bmatrix} * I$$

エッジ強度:
$$G = |P| + |Q|$$

### HLAC（2次）の定義

2次自己相関:
$$r_{ij} = \sum_{x,y} f(x,y) \cdot f(x+\Delta x_i, y+\Delta y_i) \cdot f(x+\Delta x_j, y+\Delta y_j)$$

ここで、$(\Delta x, \Delta y)$ は3×3近傍のオフセット。

### LDA判別関数

$$y = W^T x + b$$

クラス予測:
$$\hat{c} = \arg\max_c y_c$$

## FAQ

**Q: Sobelフィルタは必須ですか？**  
A: いいえ。`use_sobel=false`で無効化できます。ただし、エッジベースの識別では推奨します。

**Q: カラー画像の情報は失われませんか？**  
A: はい。グレースケール変換で色情報は失われます。色が重要な場合は、RGB各チャネルで別々に処理することを検討してください。

**Q: 25次元より多い特徴量は使えませんか？**  
A: HLAC 3次（49次元）も理論的に可能ですが、計算コストが高くなります。通常は2次（25次元）で十分です。

**Q: RA8E1での実装は？**  
A: 現在はMATLAB側の完成を優先しています。RA8E1実装は後ほど対応予定です。

## 参考文献

1. Sobel, I., & Feldman, G. (1968). "A 3x3 isotropic gradient operator for image processing"
2. Otsu, N., & Kurita, T. (1998). "A new scheme for practical flexible and intelligent vision systems"
3. Fisher, R. A. (1936). "The use of multiple measurements in taxonomic problems"

## 更新履歴

- 2026-01-17: Sobel前処理を追加
  - Sobelフィルタ実装（|P|+|Q|）
  - 可視化機能追加
  - 統計分析機能追加
  - MATLAB側の完全実装

---

詳細なシステム構成については [HLAC_LDA_README.md](HLAC_LDA_README.md) も参照してください。
