function params = load_lda_params(model_dir)
% Load LDA parameters (W, b, class_names) for inference.
%
% Prefers MATLAB-exported lda_model.mat (created by train_lda_classifier.m).
% Falls back to CSV files (lda_weights_W.csv, lda_bias_b.csv).
%
% Output:
%   params.W          [num_features x num_classes]
%   params.b          [num_classes x 1]
%   params.class_names {1 x num_classes}

if nargin < 1 || isempty(model_dir)
    model_dir = 'lda_model';
end

% Try common locations so the caller doesn't have to cd to a specific folder.
candidate_dirs = {model_dir};
try
    this_dir = fileparts(mfilename('fullpath'));
    if ~isempty(this_dir) && ~isabsolute_path(model_dir)
        candidate_dirs{end+1} = fullfile(pwd, model_dir);
        candidate_dirs{end+1} = fullfile(this_dir, model_dir);          % matlab/<model_dir>
        candidate_dirs{end+1} = fullfile(this_dir, '..', model_dir);    % <repo>/<model_dir>
    end
catch
    % ignore
end

% De-duplicate while preserving order
candidate_dirs = unique(candidate_dirs, 'stable');

selected_dir = '';
selected_mat = '';
selected_w_csv = '';
selected_b_csv = '';

for i = 1:numel(candidate_dirs)
    cand = candidate_dirs{i};
    mat_path = fullfile(cand, 'lda_model.mat');
    w_csv = fullfile(cand, 'lda_weights_W.csv');
    b_csv = fullfile(cand, 'lda_bias_b.csv');

    if exist(mat_path, 'file')
        selected_dir = cand;
        selected_mat = mat_path;
        break;
    end

    if exist(w_csv, 'file') && exist(b_csv, 'file')
        selected_dir = cand;
        selected_w_csv = w_csv;
        selected_b_csv = b_csv;
        break;
    end
end

if isempty(selected_dir)
    looked = sprintf('  - %s\n', candidate_dirs{:});
    error(['LDAモデルが見つかりません。\n' ...
           '以下のフォルダを探しました:\n%s\n' ...
           '対処: 先に学習を実行して lda_model を生成してください。例: \n' ...
           '  >> cd matlab\n' ...
           '  >> hlac_lda_workflow   (ステップ4まで実行)\n' ...
           'または lda_model フォルダに lda_model.mat / CSV を配置してください。'], looked);
end

params.model_dir = selected_dir;

if ~isempty(selected_mat)
    s = load(selected_mat);
    if isfield(s, 'W') && isfield(s, 'b')
        params.W = s.W;
        params.b = s.b;
    else
        error('lda_model.mat に W,b が見つかりません: %s', selected_mat);
    end

    % Optional: feature standardization (z-score) params
    if isfield(s, 'lda_model')
        lm = s.lda_model;
        if isstruct(lm)
            if isfield(lm, 'feature_mean')
                params.feature_mean = lm.feature_mean;
            end
            if isfield(lm, 'feature_std')
                params.feature_std = lm.feature_std;
            end
        end
    end

    if isfield(s, 'class_names')
        params.class_names = s.class_names;
    else
        % Fallback: derive from class count
        params.class_names = arrayfun(@(i) sprintf('class%d', i-1), 1:size(params.W, 2), 'UniformOutput', false);
    end

    return;
end

params.W = readmatrix(selected_w_csv);
params.b = readmatrix(selected_b_csv);
params.b = params.b(:);
params.class_names = arrayfun(@(i) sprintf('class%d', i-1), 1:size(params.W, 2), 'UniformOutput', false);
end

function tf = isabsolute_path(p)
% Works for Windows and Unix-like.
tf = false;
if isempty(p)
    return;
end
if ispc
    % Drive letter (C:\) or UNC (\\server\share)
    tf = (numel(p) >= 3 && isletter(p(1)) && p(2) == ':' && (p(3) == '\' || p(3) == '/')) || ...
         (numel(p) >= 2 && p(1) == '\' && p(2) == '\');
else
    tf = (p(1) == '/');
end
end
