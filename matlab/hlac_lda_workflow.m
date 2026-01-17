% HLAC-LDA Classification Workflow Main Script
% 
% This script guides you through the complete workflow:
% 1. Image capture
% 2. Sobel filter preprocessing
% 3. HLAC feature extraction
% 4. LDA classifier training
% 5. Parameter export for RA8E1

clear all;
close all;
clc;

fprintf('====================================\n');
fprintf('Sobel + HLAC-LDA Classification Workflow\n');
fprintf('====================================\n\n');

% Configuration
config = struct();
config.data_dir = 'hlac_training_data';
config.output_dir = 'lda_model';
config.hlac_order = 2;  % Use 2nd-order HLAC (25 dimensions)
config.use_sobel = true;  % Sobel前処理を使用
config.class_names = {'class0', 'class1', 'class2', 'class3', 'class4'};

fprintf('設定:\n');
fprintf('  データディレクトリ: %s\n', config.data_dir);
fprintf('  出力ディレクトリ: %s\n', config.output_dir);
feature_dim = (config.hlac_order==1) * 5 + (config.hlac_order==2) * 45;
fprintf('  HLAC次数: %d (特徴次元: %d)\n', config.hlac_order, feature_dim);
if config.use_sobel
    sobel_str = '有効';
else
    sobel_str = '無効';
end
fprintf('  Sobelフィルタ: %s\n', sobel_str);
fprintf('  クラス数: %d\n', length(config.class_names));
fprintf('\n');
fprintf('処理の流れ:\n');
fprintf('  画像 → Sobel(|P|+|Q|) → HLAC(25次元) → LDA分類\n');
fprintf('\n');

%% Step 1: Image Capture (Optional - Manual)
fprintf('====================================\n');
fprintf('ステップ1: 画像収集\n');
fprintf('====================================\n');
fprintf('画像を収集する必要がある場合は、以下を実行してください:\n');
fprintf('  >> hlac_image_capture\n\n');
fprintf('既に画像を収集済みの場合は、次のステップに進みます。\n\n');

choice = input('画像収集を今すぐ実行しますか? (y/n): ', 's');
if strcmpi(choice, 'y')
    fprintf('\n画像キャプチャツールを起動します...\n');
    fprintf('終了後、このスクリプトに戻ってきます。\n\n');
    hlac_image_capture();
    fprintf('\n画像収集が完了しました。\n\n');
else
    fprintf('\n画像収集をスキップします。\n\n');
end

%% Step 2: Check data
fprintf('====================================\n');
fprintf('ステップ2: データ確認\n');
fprintf('====================================\n');

if ~exist(config.data_dir, 'dir')
    error('データディレクトリが見つかりません: %s\n画像収集を先に実行してください。', config.data_dir);
end

fprintf('各クラスのサンプル数:\n');
total_samples = 0;
for i = 1:length(config.class_names)
    class_dir = fullfile(config.data_dir, config.class_names{i});
    if exist(class_dir, 'dir')
        img_files = dir(fullfile(class_dir, '*.png'));
        num_samples = length(img_files);
        fprintf('  %s: %d枚\n', config.class_names{i}, num_samples);
        total_samples = total_samples + num_samples;
    else
        fprintf('  %s: ディレクトリなし\n', config.class_names{i});
    end
end
fprintf('総サンプル数: %d枚\n\n', total_samples);

if total_samples == 0
    error('画像データが見つかりません。先に画像収集を実行してください。');
end

%% Step 3: HLAC Feature Extraction (with Sobel preprocessing)
fprintf('====================================\n');
fprintf('ステップ3: Sobel + HLAC特徴量抽出\n');
fprintf('====================================\n');

% Sobel処理の可視化オプション
choice = input('Sobel処理の効果を可視化しますか? (y/n): ', 's');
if isempty(choice)
    choice = 'n';
end
if strcmpi(choice, 'y')
    fprintf('\n各クラスから1枚ずつサンプル表示します...\n\n');
    compare_sobel_hlac_on_dataset(config.data_dir, config.class_names, 1);
    fprintf('\n可視化が完了しました。\n\n');
end

choice = input('HLAC特徴量を抽出しますか? (y/n): ', 's');
if isempty(choice)
    choice = 'y';
end

if strcmpi(choice, 'y')
    fprintf('\nSobel + HLAC特徴量を抽出中...\n\n');
    features_table = extract_hlac_from_dataset(config.data_dir, config.class_names, ...
                                               config.hlac_order, config.use_sobel);
    
    % Save features
    save('hlac_features.mat', 'features_table', 'config');
    fprintf('\n特徴量を hlac_features.mat に保存しました。\n\n');
    
    % Visualize features
    fprintf('特徴量の分布を可視化中...\n');
    try
        visualize_hlac_features(features_table, config.class_names);
    catch ME
        fprintf('可視化をスキップします: %s\n', ME.message);
    end
    
else
    fprintf('\nHLAC特徴量の抽出をスキップします。\n');
    fprintf('保存済みの特徴量を読み込んでいます...\n');
    
    if exist('hlac_features.mat', 'file')
        load('hlac_features.mat');
        fprintf('特徴量を読み込みました。\n\n');
    else
        fprintf('保存された特徴量が見つかりません。特徴量抽出を自動実行します...\n\n');
        features_table = extract_hlac_from_dataset(config.data_dir, config.class_names, ...
                                                   config.hlac_order, config.use_sobel);
        save('hlac_features.mat', 'features_table', 'config');
        fprintf('\n特徴量を hlac_features.mat に保存しました。\n\n');
    end
end

%% Step 4: LDA Training
fprintf('====================================\n');
fprintf('ステップ4: LDA分類器の学習\n');
fprintf('====================================\n');

choice = input('LDA分類器を学習しますか? (y/n): ', 's');
if isempty(choice)
    choice = 'y';
end
if strcmpi(choice, 'y')
    fprintf('\nLDA分類器を学習中...\n\n');
    [lda_model, W, b] = train_lda_classifier(features_table, config.class_names, config.output_dir);
    
    fprintf('\n学習が完了しました！\n\n');
    
    % Test inference
    fprintf('推論テストを実行中...\n');
    test_lda_inference(lda_model, features_table, config.class_names);
    
else
    fprintf('\nLDA学習をスキップします。\n\n');
    fprintf('注意: オンライン推論(hlac_udp_inference)には学習済みモデルが必要です。\n');
    fprintf('  必要ファイル: %s/lda_model.mat (または CSV)\n', config.output_dir);
    fprintf('  対処: 次回は Step4 で y を選ぶか、以下を実行してください:\n');
    fprintf('    >> [lda_model, W, b] = train_lda_classifier(features_table, config.class_names, config.output_dir);\n\n');
end

%% Step 5: Summary and Next Steps
fprintf('\n====================================\n');
fprintf('完了！\n');
fprintf('====================================\n\n');

fprintf('次のステップ:\n');
fprintf('1. 生成されたファイルを確認:\n');
fprintf('   - %s/lda_params.h (RA8E1用)\n', config.output_dir);
fprintf('   - %s/lda_weights_W.csv\n', config.output_dir);
fprintf('   - %s/lda_bias_b.csv\n', config.output_dir);
fprintf('   - %s/confusion_matrix.png\n\n', config.output_dir);

fprintf('2. RA8E1への実装:\n');
fprintf('   a. lda_params.h を RA8E1_prj/src/ にコピー\n');
fprintf('   b. hlac_extract.h, lda_inference.h を使用して推論を実装\n');
fprintf('   c. 詳細は HLAC_LDA_README.md を参照\n\n');

fprintf('3. テスト:\n');
fprintf('   RA8E1で画像を取得し、分類を実行してください。\n\n');

fprintf('====================================\n');
fprintf('ワークフロー完了\n');
fprintf('====================================\n');

%% Optional: Open documentation
choice = input('\nドキュメント(HLAC_LDA_README.md)を開きますか? (y/n): ', 's');
if strcmpi(choice, 'y')
    if ispc
        system('start ..\HLAC_LDA_README.md');
    elseif ismac
        system('open ../HLAC_LDA_README.md');
    else
        fprintf('ファイルを手動で開いてください: ../HLAC_LDA_README.md\n');
    end
end
