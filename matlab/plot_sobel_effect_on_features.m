function plot_sobel_effect_on_features(data_dir, class_names)
% Sobelフィルタが特徴量に与える影響を統計的に分析

fprintf('====================================\n');
fprintf('Sobel効果の統計分析\n');
fprintf('====================================\n\n');

fprintf('特徴量抽出中（Sobelなし）...\n');
features_table_without = extract_hlac_from_dataset(data_dir, class_names, 2, false);

fprintf('\n特徴量抽出中（Sobelあり）...\n');
features_table_with = extract_hlac_from_dataset(data_dir, class_names, 2, true);

feature_cols = features_table_without.Properties.VariableNames;
feature_cols = feature_cols(~ismember(feature_cols, {'Label', 'Filename'}));

X_without = table2array(features_table_without(:, feature_cols));
X_with = table2array(features_table_with(:, feature_cols));
labels = features_table_without.Label;

[~, score_without] = pca(X_without);
[~, score_with] = pca(X_with);

figure('Name', 'Sobel Effect Analysis', 'Position', [100, 100, 1400, 600]);

subplot(1, 2, 1);
colors = lines(length(class_names));
hold on;
for c = 0:length(class_names)-1
    idx = labels == c;
    scatter(score_without(idx, 1), score_without(idx, 2), 50, colors(c+1, :), 'filled', 'DisplayName', class_names{c+1});
end
xlabel('第1主成分');
ylabel('第2主成分');
title('特徴空間分布（Sobelなし）');
legend('Location', 'best');
grid on;
hold off;

subplot(1, 2, 2);
hold on;
for c = 0:length(class_names)-1
    idx = labels == c;
    scatter(score_with(idx, 1), score_with(idx, 2), 50, colors(c+1, :), 'filled', 'DisplayName', class_names{c+1});
end
xlabel('第1主成分');
ylabel('第2主成分');
title('特徴空間分布（Sobelあり）');
legend('Location', 'best');
grid on;
hold off;

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
