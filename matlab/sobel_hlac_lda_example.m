% Complete Example: Sobel + HLAC + LDA Classification
% 
% このスクリプトは、Sobel前処理を含むHLAC-LDA分類の
% 完全な使用例を示します。

clear all;
close all;
clc;

fprintf('====================================\n');
fprintf('Sobel + HLAC + LDA 完全な使用例\n');
fprintf('====================================\n\n');

%% 設定
class_names = {'class0', 'class1', 'class2'};  % クラス名（実際の名前に変更）
data_dir = 'hlac_training_data';
model_dir = 'lda_model';

%% ステップ1: 単一画像でSobel+HLACをテスト
fprintf('ステップ1: 単一画像のテスト\n');
fprintf('------------------------------------\n');

% テスト画像のパスを指定（実際の画像パスに変更）
test_image_path = fullfile(data_dir, class_names{1}, 'test_image.png');

if exist(test_image_path, 'file')
    fprintf('テスト画像: %s\n\n', test_image_path);
    
    % Sobel処理の可視化
    visualize_sobel_hlac_process(test_image_path);
    
    fprintf('可視化が完了しました。\n');
    fprintf('ウィンドウを確認してください。\n\n');
    
    pause(2);
else
    fprintf('テスト画像が見つかりません。\n');
    fprintf('データ収集を先に実行してください。\n\n');
end

%% ステップ2: データセット全体で特徴量抽出
fprintf('ステップ2: データセット全体の特徴量抽出\n');
fprintf('------------------------------------\n');

if ~exist(data_dir, 'dir')
    error('データディレクトリが見つかりません: %s', data_dir);
end

% Sobel + HLAC特徴量抽出
fprintf('Sobel + HLAC特徴量を抽出中...\n\n');
features_table = extract_hlac_from_dataset(data_dir, class_names, 2, true);

fprintf('\n特徴量抽出完了。\n');
fprintf('総サンプル数: %d\n', height(features_table));
fprintf('特徴次元数: 25\n\n');

% 特徴量の可視化
visualize_hlac_features(features_table, class_names);

%% ステップ3: Sobel効果の統計分析（オプション）
fprintf('ステップ3: Sobel効果の統計分析\n');
fprintf('------------------------------------\n');

choice = input('Sobelあり・なしの比較分析を実行しますか? (y/n): ', 's');
if strcmpi(choice, 'y')
    fprintf('\n比較分析を実行中...\n\n');
    plot_sobel_effect_on_features(data_dir, class_names);
    fprintf('比較分析完了。\n\n');
end

%% ステップ4: LDA分類器の学習
fprintf('ステップ4: LDA分類器の学習\n');
fprintf('------------------------------------\n');

% LDA学習
fprintf('LDA分類器を学習中...\n\n');
[lda_model, W, b] = train_lda_classifier(features_table, class_names, model_dir);

fprintf('\nLDA学習完了。\n\n');

%% ステップ5: 推論テスト
fprintf('ステップ5: 推論テスト\n');
fprintf('------------------------------------\n');

test_lda_inference(lda_model, features_table, class_names);

%% ステップ6: 新しい画像での推論例
fprintf('ステップ6: 新しい画像での推論\n');
fprintf('------------------------------------\n');

% ランダムに1枚選択して推論
if height(features_table) > 0
    rand_idx = randi(height(features_table));
    
    % 画像パス取得
    label = features_table.Label(rand_idx);
    filename = features_table.Filename{rand_idx};
    img_path = fullfile(data_dir, class_names{label+1}, filename);
    
    fprintf('テスト画像: %s\n', img_path);
    
    % 画像読み込み
    img = imread(img_path);
    
    % 特徴量抽出
    features = extract_hlac_features(img, 2, true);
    
    % LDA推論
    pred_label = predict(lda_model, features');
    
    % 結果表示
    figure('Name', '推論結果');
    subplot(1, 2, 1);
    imshow(img);
    title(sprintf('入力画像\n真のクラス: %s', class_names{label+1}));
    
    subplot(1, 2, 2);
    
    % Sobel処理画像も表示
    if size(img, 3) == 3
        gray_img = rgb2gray(img);
    else
        gray_img = img;
    end
    gray_img_norm = double(gray_img) / 255.0;
    sobel_h = [-1, 0, 1; -2, 0, 2; -1, 0, 1];
    sobel_v = [-1, -2, -1; 0, 0, 0; 1, 2, 1];
    P = conv2(gray_img_norm, sobel_h, 'same');
    Q = conv2(gray_img_norm, sobel_v, 'same');
    sobel_img = abs(P) + abs(Q);
    sobel_img = sobel_img / max(sobel_img(:));
    
    imshow(sobel_img);
    title(sprintf('Sobel処理\n予測クラス: %s', class_names{pred_label+1}));
    colorbar;
    
    fprintf('\n推論結果:\n');
    fprintf('  真のクラス: %s (label=%d)\n', class_names{label+1}, label);
    fprintf('  予測クラス: %s (label=%d)\n', class_names{pred_label+1}, pred_label);
    
    if pred_label == label
        fprintf('  結果: ✓ 正解\n');
    else
        fprintf('  結果: ✗ 不正解\n');
    end
    fprintf('\n');
end

%% 完了
fprintf('====================================\n');
fprintf('すべての処理が完了しました！\n');
fprintf('====================================\n\n');

fprintf('生成されたファイル:\n');
fprintf('  - %s/lda_params.h (RA8E1用)\n', model_dir);
fprintf('  - %s/lda_model.mat (MATLAB用)\n', model_dir);
fprintf('  - hlac_features.mat (特徴量データ)\n\n');

fprintf('次のステップ:\n');
fprintf('  1. lda_params.h をRA8E1プロジェクトにコピー\n');
fprintf('  2. RA8E1でSobel+HLAC+LDA推論を実装\n');
fprintf('  3. 詳細はHLAC_LDA_README.mdを参照\n\n');
