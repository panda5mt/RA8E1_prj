function hlac_lda_workflow(varargin)
% HLAC-LDA Classification Workflow
%
% This workflow guides you through:
% 1. Image capture
% 2. Dataset check
% 3. Sobel + HLAC feature extraction
% 4. LDA training
% 5. (Optional) parameter export for RA8E1
%
% Usage:
%   cd matlab
%   hlac_lda_workflow
%
% With options (recommended when using a refined-image folder):
%   hlac_lda_workflow('data_dir','../refined_images', 'class_names', {'class0','class1'})
%
% Options (name,value):
%   'data_dir'     (default '../hlac_training_data')
%   'output_dir'   (default '../lda_model')
%   'class_names'  (default {'class0'...'class9'})
%   'hlac_order'   (default 2)
%   'use_sobel'    (default false)
%   'do_capture'   (default false)  % run hlac_image_capture at Step1

close all;
clc;

p = inputParser;
p.addParameter('data_dir', './hlac_training_data');
p.addParameter('output_dir', './lda_model');
p.addParameter('class_names', {'class0', 'class1', 'class2', 'class3', 'class4', ...
                               'class5', 'class6', 'class7', 'class8', 'class9'});
p.addParameter('hlac_order', 2);
p.addParameter('use_sobel', false);
p.addParameter('do_capture', false);
p.parse(varargin{:});
opt = p.Results;

fprintf('====================================\n');
fprintf('Sobel + HLAC-LDA Classification Workflow\n');
fprintf('====================================\n\n');

% Configuration
config = struct();
config.data_dir = local_resolve_dir(opt.data_dir);
config.output_dir = local_resolve_dir(opt.output_dir);
config.hlac_order = opt.hlac_order;  % Use 2nd-order HLAC (25 dimensions)
config.use_sobel = opt.use_sobel;    % Sobel前処理を使用
config.class_names = opt.class_names;

fprintf('設定:\n');
fprintf('  データディレクトリ: %s\n', config.data_dir);
fprintf('  出力ディレクトリ: %s\n', config.output_dir);
feature_dim = (config.hlac_order==1) * 5 + (config.hlac_order==2) * 25;
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
if config.use_sobel
    fprintf('  画像 → Sobel → HLAC(25次元) → LDA分類\n');
else
    fprintf('  画像 → HLAC(25次元) → LDA分類\n');
end
fprintf('\n');

%% Step 1: Image Capture (Optional - Manual)
fprintf('====================================\n');
fprintf('ステップ1: 画像収集\n');
fprintf('====================================\n');
fprintf('画像を収集する必要がある場合は，以下を実行してください:\n');
fprintf('  >> hlac_image_capture\n\n');
fprintf('既に画像を収集済みの場合は，次のステップに進みます．\n\n');

choice = input('画像収集を今すぐ実行しますか? (y/n): ', 's');
if opt.do_capture || strcmpi(choice, 'y')
    fprintf('\n画像キャプチャツールを起動します...\n');
    fprintf('終了後，このスクリプトに戻ってきます．\n\n');
    hlac_image_capture();
    fprintf('\n画像収集が完了しました．\n\n');
else
    fprintf('\n画像収集をスキップします．\n\n');
end

%% Step 2: Check data
fprintf('====================================\n');
fprintf('ステップ2: データ確認\n');
fprintf('====================================\n');

if ~exist(config.data_dir, 'dir')
    error(['データディレクトリが見つかりません: %s\n' ...
           '対処: hlac_lda_workflow(''data_dir'',''<your_folder>'') で指定してください．\n' ...
           '例: hlac_lda_workflow(''data_dir'',''../hlac_training_data'')\n'], config.data_dir);
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
    error('画像データが見つかりません．先に画像収集を実行してください．');
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
    fprintf('\n可視化が完了しました．\n\n');
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
    fprintf('\n特徴量を hlac_features.mat に保存しました．\n\n');
    
    % Visualize features
    fprintf('特徴量の分布を可視化中...\n');
    try
        visualize_hlac_features(features_table, config.class_names);
    catch ME
        fprintf('可視化をスキップします: %s\n', ME.message);
    end
    
else
    fprintf('\nHLAC特徴量の抽出をスキップします．\n');
    fprintf('保存済みの特徴量を読み込んでいます...\n');
    
    if exist('hlac_features.mat', 'file')
        s = load('hlac_features.mat');
        if isfield(s, 'features_table')
            features_table = s.features_table;
        else
            error('hlac_features.mat に features_table が見つかりません');
        end

        expected_dim = (config.hlac_order==1) * 5 + (config.hlac_order==2) * 25;
        actual_dim = width(features_table) - 2; % exclude Label/Filename
        need_reextract = false;
        reasons = {};

        if actual_dim ~= expected_dim
            need_reextract = true;
            reasons{end+1} = sprintf('特徴次元 mismatch (saved=%d, expected=%d)', actual_dim, expected_dim); %#ok<AGROW>
        end

        % Validate saved config if available (prevents stale class/data settings).
        if isfield(s, 'config') && isstruct(s.config)
            old_cfg = s.config;
            if isfield(old_cfg, 'data_dir') && ~strcmp(char(old_cfg.data_dir), char(config.data_dir))
                need_reextract = true;
                reasons{end+1} = 'data_dir が前回保存時と異なる'; %#ok<AGROW>
            end
            if isfield(old_cfg, 'hlac_order') && old_cfg.hlac_order ~= config.hlac_order
                need_reextract = true;
                reasons{end+1} = 'hlac_order が前回保存時と異なる'; %#ok<AGROW>
            end
            if isfield(old_cfg, 'use_sobel') && logical(old_cfg.use_sobel) ~= logical(config.use_sobel)
                need_reextract = true;
                reasons{end+1} = 'use_sobel が前回保存時と異なる'; %#ok<AGROW>
            end
            if isfield(old_cfg, 'class_names') && ~isequal(old_cfg.class_names, config.class_names)
                need_reextract = true;
                reasons{end+1} = 'class_names が前回保存時と異なる'; %#ok<AGROW>
            end
        end

        % Validate label set against currently available class folders.
        try
            saved_labels = unique(features_table.Label(:));
            available_labels = local_available_labels(config.data_dir, config.class_names);
            if any(~ismember(saved_labels, available_labels))
                need_reextract = true;
                reasons{end+1} = sprintf('保存特徴量のラベルに現在データセットに無い値がある (saved=[%s], available=[%s])', ...
                    local_vec_to_str(saved_labels), local_vec_to_str(available_labels)); %#ok<AGROW>
            end
        catch ME
            warning('%s', sprintf('保存特徴量ラベル検証をスキップ: %s', ME.message));
        end

        if need_reextract
            fprintf('警告: 保存済み特徴量は再利用できません:\n');
            for ri = 1:numel(reasons)
                fprintf('  - %s\n', reasons{ri});
            end
            fprintf('特徴抽出を再実行します...\n\n');
            features_table = extract_hlac_from_dataset(config.data_dir, config.class_names, ...
                                                       config.hlac_order, config.use_sobel);
            save('hlac_features.mat', 'features_table', 'config');
            fprintf('\n特徴量を hlac_features.mat に保存しました．\n\n');
        else
            fprintf('特徴量を読み込みました．\n\n');
        end
    else
        fprintf('保存された特徴量が見つかりません．特徴量抽出を自動実行します...\n\n');
        features_table = extract_hlac_from_dataset(config.data_dir, config.class_names, ...
                                                   config.hlac_order, config.use_sobel);
        save('hlac_features.mat', 'features_table', 'config');
        fprintf('\n特徴量を hlac_features.mat に保存しました．\n\n');
    end
end

% Final safety check: ensure features labels match currently available dataset labels.
available_labels = local_available_labels(config.data_dir, config.class_names);
present_labels = unique(features_table.Label(:));
invalid_labels = setdiff(present_labels, available_labels);

fprintf('特徴量ラベル検証:\n');
fprintf('  available labels in dataset: [%s]\n', local_vec_to_str(available_labels));
fprintf('  present labels in features : [%s]\n', local_vec_to_str(present_labels));

if ~isempty(invalid_labels)
    fprintf('警告: 現在データセットに存在しないラベルを特徴量から除外します: [%s]\n', local_vec_to_str(invalid_labels));
    keep_mask = ismember(features_table.Label, available_labels);
    removed_n = sum(~keep_mask);
    features_table = features_table(keep_mask, :);
    fprintf('  除外サンプル数: %d\n', removed_n);

    if isempty(features_table)
        error('有効な特徴量サンプルが0件になりました。データセットと class_names を確認してください。');
    end

    save('hlac_features.mat', 'features_table', 'config');
    fprintf('  除外後の特徴量を hlac_features.mat に再保存しました。\n');
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
    [lda_model, ~, ~] = train_lda_classifier(features_table, config.class_names, config.output_dir);
    
    fprintf('\n学習が完了しました！\n\n');
    
    % Test inference
    fprintf('推論テストを実行中...\n');
    test_lda_inference(lda_model, features_table, config.class_names);
    
else
    fprintf('\nLDA学習をスキップします．\n\n');
    fprintf('注意: オンライン推論(hlac_udp_inference)には学習済みモデルが必要です．\n');
    fprintf('  必要ファイル: %s/lda_model.mat (または CSV)\n', config.output_dir);
    fprintf('  対処: 次回は Step4 で y を選ぶか，以下を実行してください:\n');
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
fprintf('   b. hlac_extract.h を使用して推論を実装\n');
fprintf('   c. 詳細は HLAC_LDA_README.md を参照\n\n');

fprintf('3. テスト:\n');
fprintf('   RA8E1で画像を取得し，分類を実行してください．\n\n');

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

end

function labels = local_available_labels(data_dir, class_names)
labels = [];
for i = 1:numel(class_names)
    d = fullfile(data_dir, class_names{i});
    if ~exist(d, 'dir')
        continue;
    end
    n = numel(dir(fullfile(d, '*.png')));
    if n > 0
        labels(end+1, 1) = i - 1; %#ok<AGROW>
    end
end
labels = unique(labels(:));
end

function s = local_vec_to_str(v)
v = v(:).';
if isempty(v)
    s = '';
    return;
end
s = strtrim(sprintf('%d ', v));
end

function resolved = local_resolve_dir(p)
% Resolve a directory path robustly:
% - If absolute path: use as-is
% - If relative: try as-is, then relative to this file, then to repo root (.. from matlab/)
if isempty(p)
    resolved = p;
    return;
end

if local_isabsolute_path(p)
    resolved = p;
    return;
end

% 1) as-is (relative to current directory)
if exist(p, 'dir')
    resolved = p;
    return;
end

% 2) relative to this .m file
this_dir = fileparts(mfilename('fullpath'));
cand = fullfile(this_dir, p);
if exist(cand, 'dir')
    resolved = cand;
    return;
end

% 3) relative to repo root (one level up from matlab/)
cand = fullfile(this_dir, '..', p);
if exist(cand, 'dir')
    resolved = cand;
    return;
end

% not found: return best-effort path so error messages are informative
resolved = cand;
end

function tf = local_isabsolute_path(p)
tf = false;
if isempty(p)
    return;
end
if ispc
    tf = (numel(p) >= 3 && isletter(p(1)) && p(2) == ':' && (p(3) == '\' || p(3) == '/')) || ...
         (numel(p) >= 2 && p(1) == '\' && p(2) == '\');
else
    tf = (p(1) == '/');
end
end
