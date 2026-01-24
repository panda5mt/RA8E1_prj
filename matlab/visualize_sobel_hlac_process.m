function visualize_sobel_hlac_process(img_path)
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

img = imread(img_path);

if size(img, 3) == 3
    gray_img = rgb2gray(img);
else
    gray_img = img;
end

gray_img_norm = double(gray_img) / 255.0;

sobel_h = [-1, 0, 1; -2, 0, 2; -1, 0, 1];
P = conv2(gray_img_norm, sobel_h, 'same');

sobel_v = [-1, -2, -1; 0, 0, 0; 1, 2, 1];
Q = conv2(gray_img_norm, sobel_v, 'same');

sobel_img = abs(P) + abs(Q);
if max(sobel_img(:)) > 0
    sobel_img_norm = sobel_img / max(sobel_img(:));
else
    sobel_img_norm = sobel_img;
end

features_with_sobel = extract_hlac_features(img, 2, true);
features_without_sobel = extract_hlac_features(img, 2, false);

figure('Name', 'Sobel + HLAC Processing Visualization', 'Position', [100, 100, 1400, 800]);

subplot(2, 3, 1);
imshow(img);
title('元画像');

subplot(2, 3, 2);
imshow(gray_img);
title('グレースケール');

subplot(2, 3, 3);
imshow(sobel_img_norm);
title('Sobel処理 (|P|+|Q|)');
colorbar;

subplot(2, 3, 4);
imshow(abs(P), []);
title('水平エッジ (|P|)');
colorbar;

subplot(2, 3, 5);
imshow(abs(Q), []);
title('垂直エッジ (|Q|)');
colorbar;

subplot(2, 3, 6);
x = 1:25;
bar(x, [features_without_sobel, features_with_sobel]);
xlabel('特徴次元');
ylabel('特徴値');
title('HLAC特徴量の比較');
legend('Sobelなし', 'Sobelあり', 'Location', 'best');
grid on;

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
fprintf('  Sobelなし - 平均: %.6f, 標準偏差: %.6f\n', mean(features_without_sobel), std(features_without_sobel));
fprintf('  Sobelあり - 平均: %.6f, 標準偏差: %.6f\n', mean(features_with_sobel), std(features_with_sobel));
fprintf('====================================\n');
end
