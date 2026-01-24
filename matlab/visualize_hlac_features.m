function visualize_hlac_features(features_table, class_names)
% HLAC特徴量の可視化(PCA使用)

feature_cols = features_table.Properties.VariableNames;
feature_cols = feature_cols(~ismember(feature_cols, {'Label', 'Filename'}));

X = table2array(features_table(:, feature_cols));
labels = features_table.Label;

try
    [~, score, ~, ~, explained] = pca(X);
catch
    % Statistics and Machine Learning Toolbox が無くても動くようにSVDでPCAを代替
    [score, explained] = local_pca_via_svd(X);
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
end

function [score, explained] = local_pca_via_svd(X)
% Toolbox-free PCA via SVD.
% score: principal component scores (same size as X)
% explained: variance explained percentage (vector)

if isempty(X)
    score = X;
    explained = [];
    return;
end

% Center columns
mu = mean(X, 1);
Xc = bsxfun(@minus, X, mu);

% SVD
[U, S, ~] = svd(Xc, 'econ');
score = U * S;

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
