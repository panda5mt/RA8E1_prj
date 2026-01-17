function test_lda_inference(lda_model, features_table, class_names)
% 推論のテスト

fprintf('\n====================================\n');
fprintf('推論テスト\n');
fprintf('====================================\n');

num_samples = min(5, size(features_table, 1));
random_indices = randperm(size(features_table, 1), num_samples);

feature_cols = features_table.Properties.VariableNames;
feature_cols = feature_cols(~ismember(feature_cols, {'Label', 'Filename'}));

for i = 1:num_samples
    idx = random_indices(i);

    features = table2array(features_table(idx, feature_cols));
    true_label = features_table.Label(idx);

    % Toolbox無しでも動くようにフォールバック
    if isstruct(lda_model) && isfield(lda_model, 'W') && isfield(lda_model, 'b')
        pred_label = lda_predict_from_Wb(features, lda_model.W, lda_model.b);
    else
        try
            pred_label = predict(lda_model, features);
        catch
            if isstruct(lda_model) && isfield(lda_model, 'type') && strcmp(lda_model.type, 'custom_lda') && isfield(lda_model, 'W') && isfield(lda_model, 'b')
                pred_label = lda_predict_from_Wb(features, lda_model.W, lda_model.b);
            else
                error('推論に必要な predict が利用できません（Statistics and Machine Learning Toolbox が必要）。');
            end
        end
    end

    fprintf('\nサンプル %d:\n', i);
    fprintf('  ファイル名: %s\n', features_table.Filename{idx});
    fprintf('  真のクラス: %s (label=%d)\n', class_names{true_label+1}, true_label);
    fprintf('  予測クラス: %s (label=%d)\n', class_names{pred_label+1}, pred_label);

    if pred_label == true_label
        fprintf('  結果: ✓ 正解\n');
    else
        fprintf('  結果: ✗ 不正解\n');
    end
end

fprintf('\n====================================\n');
end
