function [lda_model, W, b] = train_lda_classifier(features_table, class_names, output_dir)
    % LDA分類器の学習とパラメータ出力
    %
    % 入力:
    %   features_table - extract_hlac_from_datasetで生成された特徴量テーブル
    %   class_names    - クラス名のセル配列
    %   output_dir     - パラメータ出力先ディレクトリ（オプション）
    %
    % 出力:
    %   lda_model - 学習済みLDAモデル
    %   W         - LDAの重み行列
    %   b         - LDAのバイアスベクトル
    
    if nargin < 3
        output_dir = 'lda_model';
    end
    
    % 出力ディレクトリ作成
    if ~exist(output_dir, 'dir')
        mkdir(output_dir);
    end
    
    fprintf('====================================\n');
    fprintf('LDA分類器の学習開始\n');
    fprintf('====================================\n');
    
    % 特徴量とラベル抽出
    feature_cols = features_table.Properties.VariableNames;
    feature_cols = feature_cols(~ismember(feature_cols, {'Label', 'Filename'}));
    
    X = table2array(features_table(:, feature_cols));
    Y = features_table.Label;
    
    fprintf('データセット情報:\n');
    fprintf('  サンプル数: %d\n', size(X, 1));
    fprintf('  特徴次元数: %d\n', size(X, 2));
    fprintf('  クラス数: %d\n', length(unique(Y)));
    fprintf('\n');
    
    % クラスごとのサンプル数表示
    fprintf('クラス別サンプル数:\n');
    for c = 0:length(class_names)-1
        count = sum(Y == c);
        fprintf('  %s (label=%d): %d samples\n', class_names{c+1}, c, count);
    end
    fprintf('\n');

    % 利用クラス（データが存在するラベル）だけに絞り、必要ならラベルを連番化
    present_labels = unique(Y);
    present_labels = sort(present_labels(:));
    used_class_names = class_names(present_labels + 1);
    num_classes = numel(present_labels);
    if num_classes < 2
        error('LDA学習には最低2クラス必要です。現在のクラス数=%d', num_classes);
    end

    % Map original labels -> 0..K-1
    Y_mapped = zeros(size(Y));
    for i = 1:num_classes
        Y_mapped(Y == present_labels(i)) = i - 1;
    end
    
    % データを訓練とテストに分割（Toolbox無しでも動くようにフォールバック）
    holdout = 0.3;
    [train_idx, test_idx] = local_holdout_split(Y_mapped, holdout);
    X_train = X(train_idx, :);
    Y_train = Y_mapped(train_idx);
    X_test = X(test_idx, :);
    Y_test = Y_mapped(test_idx);
    
    fprintf('訓練データ: %d samples\n', size(X_train, 1));
    fprintf('テストデータ: %d samples\n\n', size(X_test, 1));
    
    % 特徴量の標準化（学習・推論で一致させる）
    [X_train_z, feat_mean, feat_std] = local_zscore_fit_transform(X_train);
    X_test_z = local_zscore_apply(X_test, feat_mean, feat_std);

    % LDA学習（Statistics and Machine Learning Toolbox が無い場合は自前実装）
    fprintf('LDA学習中...\n');
    use_builtin = (exist('fitcdiscr', 'file') == 2);
    if use_builtin
        lda_model = fitcdiscr(X_train_z, Y_train, 'DiscrimType', 'linear');
        % パラメータ抽出
        [W, b] = extract_lda_parameters(lda_model);
    else
        lda_model = struct();
        lda_model.type = 'custom_lda';
        lda_model.class_names = used_class_names;
        lda_model.present_labels = present_labels;
        [W, b] = local_train_lda_Wb(X_train_z, Y_train);
        lda_model.W = W;
        lda_model.b = b;
    end
    lda_model.feature_mean = feat_mean;
    lda_model.feature_std = feat_std;
    fprintf('学習完了\n\n');
    
    % テストデータで評価
    Y_pred_train = local_predict_labels(lda_model, W, b, X_train_z);
    Y_pred_test = local_predict_labels(lda_model, W, b, X_test_z);
    
    train_accuracy = sum(Y_pred_train == Y_train) / length(Y_train) * 100;
    test_accuracy = sum(Y_pred_test == Y_test) / length(Y_test) * 100;
    
    fprintf('====================================\n');
    fprintf('学習結果\n');
    fprintf('====================================\n');
    fprintf('訓練精度: %.2f%%\n', train_accuracy);
    fprintf('テスト精度: %.2f%%\n', test_accuracy);
    fprintf('====================================\n\n');
    
    % 混同行列表示（Toolbox無しでも動くように自前で作成）
    fprintf('混同行列（テストデータ）:\n');
    confMat = local_confusion_matrix(Y_test, Y_pred_test, num_classes);
    disp(confMat);
    fprintf('\n');
    
    fprintf('LDAパラメータ:\n');
    fprintf('  W (重み行列): %dx%d\n', size(W, 1), size(W, 2));
    fprintf('  b (バイアス): %dx1\n', size(b, 1));
    fprintf('\n');
    
    % パラメータ保存（使用クラスのみ）
    save_lda_parameters(W, b, lda_model, output_dir, used_class_names);
    
    % 混同行列を可視化（可能ならconfusionchart、無ければimagesc）
    try
        if exist('confusionchart', 'file') == 2
            figure('Name', 'Confusion Matrix');
            confusionchart(Y_test, Y_pred_test, 'RowSummary', 'row-normalized', ...
                           'ColumnSummary', 'column-normalized');
            title(sprintf('LDA Confusion Matrix (Accuracy: %.2f%%)', test_accuracy));
            saveas(gcf, fullfile(output_dir, 'confusion_matrix.png'));
        else
            figure('Name', 'Confusion Matrix');
            imagesc(confMat);
            axis image;
            colormap(parula);
            colorbar;
            xlabel('Predicted');
            ylabel('True');
            title(sprintf('LDA Confusion Matrix (Accuracy: %.2f%%)', test_accuracy));
            saveas(gcf, fullfile(output_dir, 'confusion_matrix.png'));
        end
    catch
        % visualization is optional
    end
    
    fprintf('パラメータを %s に保存しました\n', output_dir);
end

function [Xz, mu, sigma] = local_zscore_fit_transform(X)
% Fit mean/std and transform.
mu = mean(X, 1);
sigma = std(X, 0, 1);
sigma(sigma < 1e-12) = 1; % avoid divide-by-zero
Xz = bsxfun(@rdivide, bsxfun(@minus, X, mu), sigma);
end

function Xz = local_zscore_apply(X, mu, sigma)
sigma2 = sigma;
sigma2(sigma2 < 1e-12) = 1;
Xz = bsxfun(@rdivide, bsxfun(@minus, X, mu), sigma2);
end

function [train_idx, test_idx] = local_holdout_split(Y, holdout)
% Simple holdout split without cvpartition.
% Tries to keep at least 1 sample in train and test when possible.

n = numel(Y);
idx = randperm(n);

if n <= 1
    train_idx = true(n, 1);
    test_idx = false(n, 1);
    return;
end

n_test = max(1, round(n * holdout));
n_test = min(n_test, n - 1);

test_sel = idx(1:n_test);
train_sel = idx(n_test+1:end);

train_idx = false(n, 1);
test_idx = false(n, 1);
train_idx(train_sel) = true;
test_idx(test_sel) = true;
end

function [W, b] = local_train_lda_Wb(X, Y)
% Train multi-class LDA (shared covariance) and return W,b such that:
%   scores = W' * x + b
% Y must be 0..K-1.

classes = unique(Y);
classes = sort(classes(:));
K = numel(classes);
D = size(X, 2);

mu = zeros(D, K);
priors = zeros(K, 1);
Sigma = zeros(D, D);

for k = 1:K
    ck = classes(k);
    idx = (Y == ck);
    Xk = X(idx, :);
    nk = size(Xk, 1);
    priors(k) = nk;
    mu(:, k) = mean(Xk, 1).';
    if nk > 1
        Sk = cov(Xk, 1); % normalize by N
    else
        Sk = zeros(D, D);
    end
    Sigma = Sigma + (nk * Sk);
end

priors = priors / sum(priors);
Sigma = Sigma / max(1, size(X, 1));

% Regularize in case of singular covariance
lambda = 1e-3;
Sigma = Sigma + lambda * eye(D);

invSigma = pinv(Sigma);

W = invSigma * mu; % D x K

b = zeros(K, 1);
for k = 1:K
    mk = mu(:, k);
    b(k) = -0.5 * (mk.' * invSigma * mk) + log(max(priors(k), eps));
end
end

function Y_pred = local_predict_labels(lda_model, W, b, X)
% Predict labels for each row of X.

if ~isempty(lda_model) && isstruct(lda_model) && isfield(lda_model, 'type') && strcmp(lda_model.type, 'custom_lda')
    % fall through to W,b
end

if ~isempty(lda_model) && ~isstruct(lda_model) && exist('predict', 'file') == 2
    try
        Y_pred = predict(lda_model, X);
        return;
    catch
        % ignore and fall back
    end
end

n = size(X, 1);
Y_pred = zeros(n, 1);
for i = 1:n
    [pred, ~] = lda_predict_from_Wb(X(i, :), W, b);
    Y_pred(i) = pred;
end
end

function confMat = local_confusion_matrix(Y_true, Y_pred, K)
% KxK confusion matrix for labels 0..K-1.
confMat = zeros(K, K);
for i = 1:numel(Y_true)
    t = Y_true(i) + 1;
    p = Y_pred(i) + 1;
    if t >= 1 && t <= K && p >= 1 && p <= K
        confMat(t, p) = confMat(t, p) + 1;
    end
end
end

function [W, b] = extract_lda_parameters(lda_model)
    % LDAモデルから重みとバイアスを抽出
    %
    % LDAの判別関数: y = W' * x + b
    % ここで、W は特徴次元×クラス数の行列
    %       b はクラス数×1のバイアスベクトル
    
    % クラス数と特徴次元
    num_classes = length(lda_model.ClassNames);
    num_features = size(lda_model.Coeffs(1,2).Linear, 1);
    
    % 重み行列とバイアスベクトルを初期化
    W = zeros(num_features, num_classes);
    b = zeros(num_classes, 1);
    
    % 各クラスペアから係数を抽出して統合
    % MATLABのLDAは各クラスペアごとの係数を保持しているため、
    % クラス1を基準として他のクラスとの判別境界から係数を抽出
    
    for i = 1:num_classes
        if i == 1
            % 基準クラス（通常はゼロベクトル）
            W(:, i) = zeros(num_features, 1);
            b(i) = 0;
        else
            % クラス1とクラスiの判別境界から係数を取得
            coeff_struct = lda_model.Coeffs(1, i);
            W(:, i) = coeff_struct.Linear;
            b(i) = coeff_struct.Const;
        end
    end
end

function save_lda_parameters(W, b, lda_model, output_dir, class_names)
    % LDAパラメータを複数の形式で保存
    
    % 1. MATLAB形式で保存
    %    - W,b は「標準化後特徴量」に対するパラメータ
    %    - feature_mean/feature_std を一緒に保存し、推論側で同じ標準化を適用する
    save(fullfile(output_dir, 'lda_model.mat'), 'lda_model', 'W', 'b', 'class_names');
    fprintf('保存: lda_model.mat\n');
    
    % 2. CSV形式で保存（W行列）
    csvwrite(fullfile(output_dir, 'lda_weights_W.csv'), W);
    fprintf('保存: lda_weights_W.csv\n');
    
    % 3. CSV形式で保存（bベクトル）
    csvwrite(fullfile(output_dir, 'lda_bias_b.csv'), b);
    fprintf('保存: lda_bias_b.csv\n');
    
    % 4. C言語用ヘッダファイル生成
    generate_c_header(W, b, output_dir, class_names);
    fprintf('保存: lda_params.h (C言語用)\n');
    
    % 5. テキスト形式で詳細情報を保存
    fid = fopen(fullfile(output_dir, 'lda_params_info.txt'), 'w');
    fprintf(fid, 'LDA Parameters Information\n');
    fprintf(fid, '==========================\n\n');
    fprintf(fid, 'Number of classes: %d\n', length(class_names));
    fprintf(fid, 'Number of features: %d\n', size(W, 1));
    fprintf(fid, '\nClass names:\n');
    for i = 1:length(class_names)
        fprintf(fid, '  %d: %s\n', i-1, class_names{i});
    end
    fprintf(fid, '\nWeight matrix W shape: [%d x %d]\n', size(W, 1), size(W, 2));
    fprintf(fid, 'Bias vector b shape: [%d x 1]\n', size(b, 1));
    fprintf(fid, '\nUsage for inference:\n');
    fprintf(fid, '  scores = W^T * features + b\n');
    fprintf(fid, '  predicted_class = argmax(scores)\n');
    fclose(fid);
    fprintf('保存: lda_params_info.txt\n');
end

function generate_c_header(W, b, output_dir, class_names)
    % C言語用のヘッダファイルを生成
    
    [num_features, num_classes] = size(W);
    
    fid = fopen(fullfile(output_dir, 'lda_params.h'), 'w');
    
    fprintf(fid, '/*\n');
    fprintf(fid, ' * LDA Parameters for RA8E1 Inference\n');
    fprintf(fid, ' * Auto-generated by MATLAB\n');
    fprintf(fid, ' * Date: %s\n', datestr(now));
    fprintf(fid, ' */\n\n');
    fprintf(fid, '#ifndef LDA_PARAMS_H\n');
    fprintf(fid, '#define LDA_PARAMS_H\n\n');
    
    % 定数定義
    fprintf(fid, '#define LDA_NUM_FEATURES %d\n', num_features);
    fprintf(fid, '#define LDA_NUM_CLASSES %d\n\n', num_classes);
    
    % クラス名定義
    fprintf(fid, '/* Class definitions */\n');
    for i = 1:num_classes
        fprintf(fid, '#define CLASS_%s %d\n', upper(class_names{i}), i-1);
    end
    fprintf(fid, '\n');
    
    % 重み行列W（列優先）
    fprintf(fid, '/* Weight matrix W [%d x %d] (column-major) */\n', num_features, num_classes);
    fprintf(fid, 'static const float lda_weights[LDA_NUM_FEATURES * LDA_NUM_CLASSES] = {\n');
    
    for j = 1:num_classes
        fprintf(fid, '    /* Class %d (%s) */\n', j-1, class_names{j});
        for i = 1:num_features
            if i == num_features && j == num_classes
                fprintf(fid, '    %.10ff\n', W(i, j));
            else
                fprintf(fid, '    %.10ff,\n', W(i, j));
            end
        end
    end
    fprintf(fid, '};\n\n');
    
    % バイアスベクトルb
    fprintf(fid, '/* Bias vector b [%d] */\n', num_classes);
    fprintf(fid, 'static const float lda_bias[LDA_NUM_CLASSES] = {\n');
    for i = 1:num_classes
        if i == num_classes
            fprintf(fid, '    %.10ff  /* %s */\n', b(i), class_names{i});
        else
            fprintf(fid, '    %.10ff,  /* %s */\n', b(i), class_names{i});
        end
    end
    fprintf(fid, '};\n\n');
    
    % 推論関数のプロトタイプ
    fprintf(fid, '/* LDA Inference function prototype */\n');
    fprintf(fid, '/*\n');
    fprintf(fid, ' * Input:  features[LDA_NUM_FEATURES] - HLAC feature vector\n');
    fprintf(fid, ' * Output: Predicted class ID (0 to %d)\n', num_classes-1);
    fprintf(fid, ' *\n');
    fprintf(fid, ' * Example implementation:\n');
    fprintf(fid, ' *   int lda_predict(const float *features) {\n');
    fprintf(fid, ' *       float scores[LDA_NUM_CLASSES];\n');
    fprintf(fid, ' *       \n');
    fprintf(fid, ' *       // Compute scores = W^T * features + b\n');
    fprintf(fid, ' *       for (int c = 0; c < LDA_NUM_CLASSES; c++) {\n');
    fprintf(fid, ' *           scores[c] = lda_bias[c];\n');
    fprintf(fid, ' *           for (int f = 0; f < LDA_NUM_FEATURES; f++) {\n');
    fprintf(fid, ' *               scores[c] += lda_weights[c * LDA_NUM_FEATURES + f] * features[f];\n');
    fprintf(fid, ' *           }\n');
    fprintf(fid, ' *       }\n');
    fprintf(fid, ' *       \n');
    fprintf(fid, ' *       // Find argmax\n');
    fprintf(fid, ' *       int max_class = 0;\n');
    fprintf(fid, ' *       float max_score = scores[0];\n');
    fprintf(fid, ' *       for (int c = 1; c < LDA_NUM_CLASSES; c++) {\n');
    fprintf(fid, ' *           if (scores[c] > max_score) {\n');
    fprintf(fid, ' *               max_score = scores[c];\n');
    fprintf(fid, ' *               max_class = c;\n');
    fprintf(fid, ' *           }\n');
    fprintf(fid, ' *       }\n');
    fprintf(fid, ' *       \n');
    fprintf(fid, ' *       return max_class;\n');
    fprintf(fid, ' *   }\n');
    fprintf(fid, ' */\n\n');
    
    fprintf(fid, '#endif /* LDA_PARAMS_H */\n');
    
    fclose(fid);
end
