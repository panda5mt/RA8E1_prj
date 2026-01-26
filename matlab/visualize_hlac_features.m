function visualize_hlac_features(features_table, class_names, varargin)
% HLAC特徴量の可視化(PCA使用)
%
% 追加オプション (name,value):
%   'topN'               (default 10)  PC1/PC2で寄与が大きい次元の表示数
%   'showLoadingsPlot'   (default true) 寄与(loading)の棒グラフを表示
%   'standardize'        (default false) PCA前に各特徴をz-score標準化

p = inputParser;
p.addParameter('topN', 10);
p.addParameter('showLoadingsPlot', true);
p.addParameter('standardize', true);
p.parse(varargin{:});
opt = p.Results;

feature_cols = features_table.Properties.VariableNames;
feature_cols = feature_cols(~ismember(feature_cols, {'Label', 'Filename'}));

X = table2array(features_table(:, feature_cols));
labels = features_table.Label;

X_pca = X;
if opt.standardize
    mu = mean(X_pca, 1);
    sigma = std(X_pca, 0, 1);
    sigma(sigma < 1e-12) = 1;
    X_pca = bsxfun(@rdivide, bsxfun(@minus, X_pca, mu), sigma);
end

try
    [coeff, score, ~, ~, explained] = pca(X_pca);
catch
    % Statistics and Machine Learning Toolbox が無くても動くようにSVDでPCAを代替
    [coeff, score, explained] = local_pca_via_svd(X_pca);
end

figure('Name', 'HLAC特徴量の分布(PCA)');
hold on;

colors = lines(length(class_names));
for c = 0:length(class_names)-1
    idx = labels == c;
    scatter(score(idx, 1), score(idx, 2), 50, colors(c+1, :), 'filled', ...
        'DisplayName', class_names{c+1});
end

xlabel(sprintf('第1主成分 (%.1f%%)', explained(1)));
ylabel(sprintf('第2主成分 (%.1f%%)', explained(2)));
title('HLAC特徴量の分布');
legend('Location', 'best');
grid on;
hold off;

% --- PC1/PC2の寄与(loading)表示 ---
topN = max(1, min(opt.topN, numel(feature_cols)));
if ~isempty(coeff) && size(coeff, 2) >= 2
    fprintf('\n====================================\n');
    fprintf('PCA Loading 上位%d次元 (|loading| が大きい順)\n', topN);
    fprintf('※ loading は「各元特徴がPC軸にどれだけ効いているか」の重みです\n');
    if opt.standardize
        fprintf('※ standardize=true のため，各特徴をz-score標準化してからPCAしています\n');
    end
    fprintf('====================================\n');

    local_print_top_loadings(feature_cols, coeff(:, 1), topN, 1);
    local_print_top_loadings(feature_cols, coeff(:, 2), topN, 2);

    if opt.showLoadingsPlot
        figure('Name', sprintf('PCA Loadings (Top %d)', topN));
        tiledlayout(2, 1, 'Padding', 'compact', 'TileSpacing', 'compact');

        nexttile;
        local_plot_top_loadings(feature_cols, coeff(:, 1), topN, 'PC1');

        nexttile;
        local_plot_top_loadings(feature_cols, coeff(:, 2), topN, 'PC2');
    end
end
end

function [coeff, score, explained] = local_pca_via_svd(X)
% Toolbox-free PCA via SVD.
% score: principal component scores (same size as X)
% explained: variance explained percentage (vector)

if isempty(X)
    coeff = [];
    score = X;
    explained = [];
    return;
end

% Center columns
mu = mean(X, 1);
Xc = bsxfun(@minus, X, mu);

% SVD
[U, S, V] = svd(Xc, 'econ');
score = U * S;
coeff = V;

% Explained variance
svals = diag(S);
n = size(Xc, 1);
if n > 1
    latent = (svals .^ 2) / (n - 1);
else
    latent = (svals .^ 2);
end

total = sum(latent);
if total > 0
    explained = 100 * (latent / total);
else
    explained = zeros(size(latent));
end
end

function local_print_top_loadings(feature_cols, loading_vec, topN, pcIndex)
loading_vec = loading_vec(:);
[~, order] = sort(abs(loading_vec), 'descend');
order = order(1:topN);

fprintf('\n[PC%d] 上位%d: (feature, loading)\n', pcIndex, topN);
for i = 1:numel(order)
    k = order(i);
    fprintf('  %2d) %s : %+0.6f\n', i, feature_cols{k}, loading_vec(k));
end
end

function local_plot_top_loadings(feature_cols, loading_vec, topN, titlePrefix)
loading_vec = loading_vec(:);
[~, order] = sort(abs(loading_vec), 'descend');
order = order(1:topN);

vals = loading_vec(order);
names = string(feature_cols(order));

bar(vals);
grid on;
title(sprintf('%s loadings (Top %d)', titlePrefix, topN));
ylabel('loading');
xticks(1:topN);
xticklabels(names);
xtickangle(45);
end
