function visualize_sobel_hlac(img_path)
    % Sobel + HLAC処理の可視化
    %
    % 入力:
    %   img_path - 画像ファイルパス
    %
    % 表示内容:
    %   1. 元画像
    %   2. グレースケール画像
    %   3. Sobel処理画像(|P|+|Q|)
    %   4. HLAC特徴量のバー表示
    
    % 画像読み込み
    img = imread(img_path);
    
    % グレースケール変換
    if size(img, 3) == 3
        gray_img = rgb2gray(img);
    else
        gray_img = img;
    end
    
    % 正規化
    gray_img_norm = double(gray_img) / 255.0;
    
    % Sobelフィルタ適用
    % 水平Sobel
    sobel_h = [-1, 0, 1; -2, 0, 2; -1, 0, 1];
    P = conv2(gray_img_norm, sobel_h, 'same');
    
    % 垂直Sobel
    sobel_v = [-1, -2, -1; 0, 0, 0; 1, 2, 1];
    Q = conv2(gray_img_norm, sobel_v, 'same');
    
    % エッジ強度
    sobel_img = abs(P) + abs(Q);
    if max(sobel_img(:)) > 0
        sobel_img_norm = sobel_img / max(sobel_img(:));
    else
        sobel_img_norm = sobel_img;
    end
    
    % HLAC特徴量抽出(Sobelあり・なし)
    features_with_sobel = extract_hlac_features(img, 2, true);
    features_without_sobel = extract_hlac_features(img, 2, false);
    
    % 可視化
    figure('Name', 'Sobel + HLAC Processing Visualization', 'Position', [100, 100, 1400, 800]);
    
    % 元画像
    subplot(2, 3, 1);
    imshow(img);
    title('元画像');
    
    % グレースケール
    subplot(2, 3, 2);
    imshow(gray_img);
    title('グレースケール');
    
    % Sobel処理画像
    subplot(2, 3, 3);
    imshow(sobel_img_norm);
    title('Sobel処理 (|P|+|Q|)');
    colorbar;
    
    % 水平エッジ(P)
    subplot(2, 3, 4);
    imshow(abs(P), []);
    title('水平エッジ (|P|)');
    colorbar;
    
    % 垂直エッジ(Q)
    subplot(2, 3, 5);
    imshow(abs(Q), []);
    title('垂直エッジ (|Q|)');
    colorbar;
    
    % HLAC特徴量比較
    subplot(2, 3, 6);
    x = 1:25;
    bar(x, [features_without_sobel, features_with_sobel]);
    xlabel('特徴次元');
    ylabel('特徴値');
    title('HLAC特徴量の比較');
    legend('Sobelなし', 'Sobelあり', 'Location', 'best');
    grid on;
    
    % 統計情報表示
    fprintf('====================================\n');
    fprintf('画像処理統計\n');
    fprintf('====================================\n');
    fprintf('画像サイズ: %dx%d\n', size(gray_img, 2), size(gray_img, 1));
    fprintf('Sobelエッジ強度:\n');
    fprintf('  最小値: %.4f\n', min(sobel_img(:)));
    fprintf('  最大値: %.4f\n', max(sobel_img(:)));
    fprintf('  平均値: %.4f\n', mean(sobel_img(:)));
    fprintf('  標準偏差: %.4f\n', std(sobel_img(:)));
    fprintf('\nHLAC特徴量:\n');
    fprintf('  Sobelなし - 平均: %.6f, 標準偏差: %.6f\n', ...
            mean(features_without_sobel), std(features_without_sobel));
    fprintf('  Sobelあり - 平均: %.6f, 標準偏差: %.6f\n', ...
            mean(features_with_sobel), std(features_with_sobel));
    fprintf('====================================\n');
end

function compare_sobel_hlac_on_dataset(data_dir, class_names, num_samples_per_class)
    % データセットでSobelあり・なしの効果を比較
    %
    % 入力:
    %   data_dir              - データセットディレクトリ
    %   class_names           - クラス名のセル配列
    %   num_samples_per_class - 各クラスから表示するサンプル数(デフォルト=3)
    
    if nargin < 3
        num_samples_per_class = 3;
    end
    
    fprintf('====================================\n');
    fprintf('Sobel効果の比較\n');
    fprintf('====================================\n\n');
    
    for c = 1:length(class_names)
        class_name = class_names{c};
        class_dir = fullfile(data_dir, class_name);
        
        if ~exist(class_dir, 'dir')
            continue;
        end
        
        % 画像ファイル取得
        img_files = dir(fullfile(class_dir, '*.png'));
        
        fprintf('クラス: %s\n', class_name);
        
        % ランダムにサンプルを選択
        num_available = min(num_samples_per_class, length(img_files));
        if num_available == 0
            fprintf('  画像なし\n\n');
            continue;
        end
        
        sample_indices = randperm(length(img_files), num_available);
        
        for i = 1:num_available
            img_path = fullfile(class_dir, img_files(sample_indices(i)).name);
            fprintf('  サンプル %d: %s\n', i, img_files(sample_indices(i)).name);
            
            % 可視化
            visualize_sobel_hlac(img_path);
            
            % ウィンドウタイトル更新
            set(gcf, 'Name', sprintf('%s - %s', class_name, img_files(sample_indices(i)).name));
        end
        
        fprintf('\n');
    end
end

function plot_sobel_effect_on_features(data_dir, class_names)
    % Sobelフィルタが特徴量に与える影響を統計的に分析
    
    fprintf('====================================\n');
    fprintf('Sobel効果の統計分析\n');
    fprintf('====================================\n\n');
    
    % Sobelあり・なしで特徴量抽出
    fprintf('特徴量抽出中(Sobelなし)...\n');
    features_table_without = extract_hlac_from_dataset(data_dir, class_names, 2, false);
    
    fprintf('\n特徴量抽出中(Sobelあり)...\n');
    features_table_with = extract_hlac_from_dataset(data_dir, class_names, 2, true);
    
    % 特徴量データ取得
    feature_cols = features_table_without.Properties.VariableNames;
    feature_cols = feature_cols(~ismember(feature_cols, {'Label', 'Filename'}));
    
    X_without = table2array(features_table_without(:, feature_cols));
    X_with = table2array(features_table_with(:, feature_cols));
    labels = features_table_without.Label;
    
    % PCA分析
    [~, score_without] = pca(X_without);
    [~, score_with] = pca(X_with);
    
    % プロット
    figure('Name', 'Sobel Effect Analysis', 'Position', [100, 100, 1400, 600]);
    
    % Sobelなし
    subplot(1, 2, 1);
    colors = lines(length(class_names));
    hold on;
    for c = 0:length(class_names)-1
        idx = labels == c;
        scatter(score_without(idx, 1), score_without(idx, 2), 50, colors(c+1, :), ...
                'filled', 'DisplayName', class_names{c+1});
    end
    xlabel('第1主成分');
    ylabel('第2主成分');
    title('特徴空間分布(Sobelなし)');
    legend('Location', 'best');
    grid on;
    hold off;
    
    % Sobelあり
    subplot(1, 2, 2);
    hold on;
    for c = 0:length(class_names)-1
        idx = labels == c;
        scatter(score_with(idx, 1), score_with(idx, 2), 50, colors(c+1, :), ...
                'filled', 'DisplayName', class_names{c+1});
    end
    xlabel('第1主成分');
    ylabel('第2主成分');
    title('特徴空間分布(Sobelあり)');
    legend('Location', 'best');
    grid on;
    hold off;
    
    % 統計情報
    fprintf('\n====================================\n');
    fprintf('特徴量統計\n');
    fprintf('====================================\n');
    fprintf('Sobelなし:\n');
    fprintf('  平均: %.6f\n', mean(X_without(:)));
    fprintf('  標準偏差: %.6f\n', std(X_without(:)));
    fprintf('  範囲: [%.6f, %.6f]\n', min(X_without(:)), max(X_without(:)));
    fprintf('\nSobelあり:\n');
    fprintf('  平均: %.6f\n', mean(X_with(:)));
    fprintf('  標準偏差: %.6f\n', std(X_with(:)));
    fprintf('  範囲: [%.6f, %.6f]\n', min(X_with(:)), max(X_with(:)));
    fprintf('====================================\n');
end
