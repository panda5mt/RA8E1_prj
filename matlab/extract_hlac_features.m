function features = extract_hlac_features(img, order, use_sobel)
    % HLAC（Higher-order Local Auto-Correlation）特徴量抽出
    % Sobelフィルタ前処理を含む
    %
    % 入力:
    %   img        - 入力画像（グレースケールまたはRGB）
    %   order      - HLAC次数（1または2、デフォルトは2）
    %   use_sobel  - Sobelフィルタを適用するか（デフォルトはtrue）
    %
    % 出力:
    %   features - HLAC特徴ベクトル
    %            order=1: 5次元
    %            order=2: 25次元（0次(1) + 1次(4) + 2次(20)）
    %
    % 処理の流れ:
    %   1. グレースケール変換
    %   2. Sobelフィルタ適用（|P|+|Q|を計算）
    %   3. HLAC特徴量抽出
    
    if nargin < 2
        order = 2;  % デフォルトは2次
    end
    
    if nargin < 3
        use_sobel = true;  % デフォルトでSobelフィルタを使用
    end
    
    % グレースケール変換
    if size(img, 3) == 3
        gray_img = rgb2gray(img);
    else
        gray_img = img;
    end
    
    % 正規化
    gray_img = double(gray_img) / 255.0;
    
    % Sobelフィルタ適用
    if use_sobel
        sobel_img = apply_sobel_filter(gray_img);
    else
        sobel_img = gray_img;
    end
    
    % HLAC特徴量抽出
    if order == 1
        features = compute_hlac_order1(sobel_img);
    elseif order == 2
        features = compute_hlac_order2(sobel_img);
    else
        error('サポートされていない次数です。order=1または2を指定してください。');
    end
end

function sobel_img = apply_sobel_filter(img)
    % Sobelフィルタを適用して|P|+|Q|を計算
    %
    % 入力:
    %   img - 正規化済みグレースケール画像 [0, 1]
    %
    % 出力:
    %   sobel_img - Sobelエッジ強度画像 [0, 1]
    
    % Sobelカーネル定義
    % 水平方向（P）
    sobel_h = [-1, 0, 1;
               -2, 0, 2;
               -1, 0, 1];
    
    % 垂直方向（Q）
    sobel_v = [-1, -2, -1;
                0,  0,  0;
                1,  2,  1];
    
    % 畳み込み演算
    P = conv2(img, sobel_h, 'same');
    Q = conv2(img, sobel_v, 'same');
    
    % エッジ強度計算: |P| + |Q|
    sobel_img = abs(P) + abs(Q);
    
    % 正規化（0-1範囲）
    if max(sobel_img(:)) > 0
        sobel_img = sobel_img / max(sobel_img(:));
    end
end

function features = compute_hlac_order1(img)
    % 1次HLAC特徴量（5次元）
    % パターン:
    % 1. r0 = f(r)
    % 2. r1 = f(r)*f(r+a1) (右)
    % 3. r2 = f(r)*f(r+a2) (下)
    % 4. r3 = f(r)*f(r+a3) (右下)
    % 5. r4 = f(r)*f(r+a4) (右上)
    
    [h, w] = size(img);
    features = zeros(5, 1);
    
    % パディング（Toolbox無しでも動くように自前実装）
    padded = local_padarray_zeros(img, 1, 1);
    
    count = 0;
    
    for y = 2:h+1
        for x = 2:w+1
            center = padded(y, x);
            
            % r0: 自己相関
            features(1) = features(1) + center;
            
            % r1: 右
            right = padded(y, x+1);
            features(2) = features(2) + center * right;
            
            % r2: 下
            down = padded(y+1, x);
            features(3) = features(3) + center * down;
            
            % r3: 右下
            right_down = padded(y+1, x+1);
            features(4) = features(4) + center * right_down;
            
            % r4: 右上
            right_up = padded(y-1, x+1);
            features(5) = features(5) + center * right_up;
            
            count = count + 1;
        end
    end
    
    % 正規化
    features = features / count;
end

function features = compute_hlac_order2(img)
    % 2次HLAC特徴量（25次元）
    % 3x3近傍の定義に基づく「0次(1) + 1次(4) + 2次(20)」
    % 2次(20)は、8近傍(中心除く)のペア(重複あり)をD4対称性で同値類にまとめた代表パターン
    
    [h, w] = size(img);
    features = zeros(25, 1);
    
    % パディング（Toolbox無しでも動くように自前実装）
    padded = local_padarray_zeros(img, 1, 1);
    
    % 8近傍オフセット（中心除外）
    persistent offsets8 pair_idx idx_right idx_down idx_rd idx_ru
    if isempty(offsets8)
        offsets8 = [
            -1, -1;  % 左上
            -1,  0;  % 上
            -1,  1;  % 右上
             0, -1;  % 左
             0,  1;  % 右
             1, -1;  % 左下
             1,  0;  % 下
             1,  1   % 右下
        ];

        idx_right = find(offsets8(:, 1) == 0 & offsets8(:, 2) == 1, 1);
        idx_down  = find(offsets8(:, 1) == 1 & offsets8(:, 2) == 0, 1);
        idx_rd    = find(offsets8(:, 1) == 1 & offsets8(:, 2) == 1, 1);
        idx_ru    = find(offsets8(:, 1) == -1 & offsets8(:, 2) == 1, 1);

        % 2次(20)のパターンをD4対称でまとめて代表を選ぶ
        pair_idx = local_hlac25_second_order_pairs(offsets8);
    end

    count = 0;
    neigh = zeros(8, 1);
    for y = 2:h+1
        for x = 2:w+1
            center = padded(y, x);

            for k = 1:8
                neigh(k) = padded(y + offsets8(k, 1), x + offsets8(k, 2));
            end

            % 0次 + 1次 (5次元)
            features(1) = features(1) + center;
            features(2) = features(2) + center * neigh(idx_right);
            features(3) = features(3) + center * neigh(idx_down);
            features(4) = features(4) + center * neigh(idx_rd);
            features(5) = features(5) + center * neigh(idx_ru);

            % 2次 (20次元)
            for p = 1:size(pair_idx, 1)
                i = pair_idx(p, 1);
                j = pair_idx(p, 2);
                features(5 + p) = features(5 + p) + center * neigh(i) * neigh(j);
            end

            count = count + 1;
        end
    end

    features = features / count;
end

function padded = local_padarray_zeros(img, pad_h, pad_w)
% Minimal padarray replacement: pads with zeros on both sides.
if nargin < 2, pad_h = 1; end
if nargin < 3, pad_w = pad_h; end

[h, w] = size(img);
padded = zeros(h + 2*pad_h, w + 2*pad_w);
padded(1+pad_h:pad_h+h, 1+pad_w:pad_w+w) = img;
end

function pair_idx = local_hlac25_second_order_pairs(offsets8)
% Returns 20 representative unordered pairs (i<=j) of the 8-neighborhood offsets,
% reduced by D4 symmetry. Output is stable (sorted by canonical key).

% All unordered pairs with repetition
pairs_all = zeros(36, 2);
z = 1;
for i = 1:8
    for j = i:8
        pairs_all(z, :) = [i, j];
        z = z + 1;
    end
end

T = local_d4_transforms();
keys = cell(size(pairs_all, 1), 1);

for p = 1:size(pairs_all, 1)
    i = pairs_all(p, 1);
    j = pairs_all(p, 2);
    a = offsets8(i, :);
    b = offsets8(j, :);

    best = '';
    for t = 1:size(T, 3)
        a2 = (T(:, :, t) * a.').';
        b2 = (T(:, :, t) * b.').';
        % order the pair
        if local_lex_gt(a2, b2)
            tmp = a2; a2 = b2; b2 = tmp;
        end
        s = sprintf('%d,%d;%d,%d', a2(1), a2(2), b2(1), b2(2));
        if isempty(best) || local_str_lt(s, best)
            best = s;
        end
    end
    keys{p} = best;
end

% Group by canonical key
[keys_sorted, order] = sort(keys);
pairs_sorted = pairs_all(order, :);

pair_idx = zeros(0, 2);
last_key = '';
for p = 1:numel(keys_sorted)
    if ~strcmp(keys_sorted{p}, last_key)
        pair_idx(end+1, :) = pairs_sorted(p, :); %#ok<AGROW>
        last_key = keys_sorted{p};
    end
end

function tf = local_str_lt(a, b)
% True if string/char a is lexicographically smaller than b.
ab = sort({a, b});
tf = strcmp(ab{1}, a);
end

% Expect 20 patterns
if size(pair_idx, 1) ~= 20
    error('HLAC 2nd-order patterns expected 20, got %d. Please verify D4 reduction.', size(pair_idx, 1));
end
end

function tf = local_lex_gt(a, b)
% True if a is lexicographically greater than b.
tf = (a(1) > b(1)) || (a(1) == b(1) && a(2) > b(2));
end

function T = local_d4_transforms()
% 8 transforms of the dihedral group D4.
T = zeros(2, 2, 8);
T(:, :, 1) = [ 1  0;  0  1]; % identity
T(:, :, 2) = [ 0 -1;  1  0]; % rot90
T(:, :, 3) = [-1  0;  0 -1]; % rot180
T(:, :, 4) = [ 0  1; -1  0]; % rot270
T(:, :, 5) = [-1  0;  0  1]; % reflect y-axis
T(:, :, 6) = [ 1  0;  0 -1]; % reflect x-axis
T(:, :, 7) = [ 0  1;  1  0]; % reflect diagonal
T(:, :, 8) = [ 0 -1; -1  0]; % reflect other diagonal
end
