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
    %            order=2: 45次元（3x3近傍の組合せ i<=j を全列挙）
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
    
    % パディング
    padded = padarray(img, [1, 1], 0, 'both');
    
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
    % 2次HLAC特徴量（45次元）
    % 3x3近傍(9点)の組合せ i<=j を全列挙
    
    [h, w] = size(img);
    features = zeros(45, 1);
    
    % パディング
    padded = padarray(img, [1, 1], 0, 'both');
    
    % 近傍オフセット定義（3x3）
    offsets = [
        -1, -1;  % 左上
        -1,  0;  % 上
        -1,  1;  % 右上
         0, -1;  % 左
         0,  0;  % 中心
         0,  1;  % 右
         1, -1;  % 左下
         1,  0;  % 下
         1,  1   % 右下
    ];

    feature_idx = 1;
    
    % 2次自己相関パターン生成
    for i = 1:9
        for j = i:9
            sum_val = 0;
            
            for y = 2:h+1
                for x = 2:w+1
                    % 中心ピクセル
                    center = padded(y, x);
                    
                    % パターン位置
                    pos1 = padded(y + offsets(i, 1), x + offsets(i, 2));
                    pos2 = padded(y + offsets(j, 1), x + offsets(j, 2));
                    
                    % 2次自己相関
                    sum_val = sum_val + center * pos1 * pos2;
                end
            end
            
            features(feature_idx) = sum_val;
            feature_idx = feature_idx + 1;
        end
    end
    
    % 正規化
    features = features / (h * w);
end
