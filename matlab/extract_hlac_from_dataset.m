function features_table = extract_hlac_from_dataset(data_dir, class_names, order, use_sobel)
% データセット全体からHLAC特徴量を抽出(任意でSobel前処理)
%
% 入力:
%   data_dir    - データセットディレクトリ
%   class_names - クラス名のセル配列
%   order       - HLAC次数(デフォルト=2)
%   use_sobel   - Sobelフィルタを使用するか(デフォルト=false)
%
% 出力:
%   features_table - 特徴量とラベルを含むテーブル

if nargin < 3
    order = 2;
end

if nargin < 4
    use_sobel = false;
end

all_features = [];
all_labels = [];
all_filenames = {};

fprintf('====================================\n');
fprintf('HLAC特徴量抽出\n');
fprintf('====================================\n');
if use_sobel
    sobel_str = '有効';
else
    sobel_str = '無効';
end
fprintf('Sobelフィルタ: %s\n', sobel_str);
fprintf('HLAC次数: %d\n', order);
fprintf('特徴次元数: %d\n', (order==1) * 5 + (order==2) * 25);
fprintf('====================================\n\n');

for c = 1:length(class_names)
    class_name = class_names{c};
    class_dir = fullfile(data_dir, class_name);

    if ~exist(class_dir, 'dir')
        fprintf('警告: ディレクトリが見つかりません: %s\n', class_dir);
        continue;
    end

    img_files = dir(fullfile(class_dir, '*.png'));

    fprintf('クラス %s: %d枚の画像を処理中...\n', class_name, length(img_files));

    for i = 1:length(img_files)
        img_path = fullfile(class_dir, img_files(i).name);

        try
            img = imread(img_path);
            features = extract_hlac_features(img, order, use_sobel);

            all_features = [all_features; features'];
            all_labels = [all_labels; c-1];
            all_filenames{end+1} = img_files(i).name;

            if mod(i, 10) == 0
                fprintf('  進捗: %d/%d\n', i, length(img_files));
            end
        catch ME
            fprintf('警告: 画像処理エラー (%s): %s\n', img_files(i).name, ME.message);
        end
    end
end

fprintf('特徴量抽出完了: 総サンプル数 = %d\n', size(all_features, 1));

features_table = array2table(all_features);
features_table.Label = all_labels;
features_table.Filename = all_filenames';
end
