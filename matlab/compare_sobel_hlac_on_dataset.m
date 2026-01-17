function compare_sobel_hlac_on_dataset(data_dir, class_names, num_samples_per_class)
% データセットでSobelあり・なしの効果を比較
%
% 入力:
%   data_dir              - データセットディレクトリ
%   class_names           - クラス名のセル配列
%   num_samples_per_class - 各クラスから表示するサンプル数（デフォルト=3）

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

    img_files = dir(fullfile(class_dir, '*.png'));

    fprintf('クラス: %s\n', class_name);

    num_available = min(num_samples_per_class, length(img_files));
    if num_available == 0
        fprintf('  画像なし\n\n');
        continue;
    end

    sample_indices = randperm(length(img_files), num_available);

    for i = 1:num_available
        img_path = fullfile(class_dir, img_files(sample_indices(i)).name);
        fprintf('  サンプル %d: %s\n', i, img_files(sample_indices(i)).name);

        visualize_sobel_hlac_process(img_path);
        set(gcf, 'Name', sprintf('%s - %s', class_name, img_files(sample_indices(i)).name));
    end

    fprintf('\n');
end
end
