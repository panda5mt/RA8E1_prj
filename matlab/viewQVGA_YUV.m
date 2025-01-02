function viewQVGAfromYUV(filename)
    %{
    TeraTermログ 'filename' を読み込み、
    "!srt" 以降にある "0xXXXXXXXX" (32bit) を 2ピクセル=YUV422(YUYV) としてパース。
    QVGA(320×240) 1フレームを RGB画像として表示するサンプル。

    【前提】
     - 1行32bit が 2画素ぶん 
       Byte0=Y0, Byte1=U0, Byte2=Y1, Byte3=V0 (YUYV フォーマット)
     - 1フレーム = 76800ピクセル → 38400行(32bit)
    %}

    %==============================
    % 1) テキストファイル全行を読み込み
    %==============================
    lines = readlines(filename, 'EmptyLineRule','skip'); % R2020b 以降
    
    %==============================
    % 2) "!srt" が出現する行を探す
    %==============================
    startIndex = find(contains(lines, '!srt'), 1, 'first');
    if isempty(startIndex)
        error('ファイル内に "!srt" が見つかりませんでした。');
    end
    
    %==============================
    % 3) "!srt" 以降の行を抽出し、"0xXXXXXXXX" のみ残す
    %==============================
    dataLines = lines(startIndex+1:end);
    dataLines = dataLines(startsWith(dataLines, '0x'));
    
    %==============================
    % 4) 16進数 → 10進数(uint32) に変換
    %==============================
    hexVals_dbl = cellfun(@(s) sscanf(s, '0x%X'), dataLines, 'UniformOutput', true);
    hexVals_u32 = uint32(hexVals_dbl);
    
    %==============================
    % 5) 行数チェック & 必要な分だけ切り出す
    %==============================
    % QVGA: 320×240 = 76800 ピクセル
    % 1行(32bit)=2ピクセル → 38400行必要
    requiredLines = 320 * 240 / 2; % 38400
    if length(hexVals_u32) < requiredLines
        error('データ行数が不足しています (必要: %d, 実際: %d)', ...
            requiredLines, length(hexVals_u32));
    end
    hexVals_u32 = hexVals_u32(1:requiredLines);

    %==============================
    % 6) YUV422 (YUYV) 取り出し
    %==============================
    % hexVals_u32(i) = 0xYYUYYV (Byte順: Y0 U0 Y1 V0)
    % → 2画素分
    %  ピクセル1: (Y0, U0, V0)
    %  ピクセル2: (Y1, U0, V0)
    % 
    % それぞれを配列に展開していく
    % ※ length = 38400 (行) → ピクセル = 76800
    nLines = requiredLines; % 38400
    nPixels = 2 * nLines;   % 76800
    
    % まず uint8配列を作る (要素数 76800)
    Y_ch = zeros(nPixels,1,'uint8');
    U_ch = zeros(nPixels,1,'uint8');
    V_ch = zeros(nPixels,1,'uint8');
    
    for i = 1:2:nLines
        val = hexVals_u32(i);           % 0xYYUYYV (32bit)
        val2 = hexVals_u32(i+1);           % 0xYYUYYV (32bit)
        
        % Byteを1つずつ取り出す (上位→下位の順; 要エンディアン注意)
        %   bitand(val, 0xFF000000) >> 24 = Byte0 (Y0)
        %   bitand(val, 0x00FF0000) >> 16 = Byte1 (U0)
        %   bitand(val, 0x0000FF00) >>  8 = Byte2 (Y1)
        %   bitand(val, 0x000000FF)      = Byte3 (V0)
        % Y0 = uint8(bitshift(bitand(val, 0xFF000000), -24));
        % U0 = uint8(bitshift(bitand(val, 0x00FF0000), -16));
        % Y1 = uint8(bitshift(bitand(val, 0x0000FF00),  -8));
        % V0 = uint8(bitand(val, 0x000000FF));
        Y0 = uint8(bitshift(bitand(val, 0xFF000000), -24));
        U0 = uint8(bitshift(bitand(val, 0x00FF0000), -16));
        Y1 = uint8(bitshift(bitand(val, 0x0000FF00),  -8));
        V0 = uint8(bitand(val, 0x000000FF));

        Y2 = uint8(bitshift(bitand(val2, 0xFF000000), -24));
        U1 = uint8(bitshift(bitand(val2, 0x00FF0000), -16));
        Y3 = uint8(bitshift(bitand(val2, 0x0000FF00),  -8));
        V1 = uint8(bitand(val2, 0x000000FF));
        % ピクセル番号を2つ割り当て (ピクセル1, ピクセル2)
        pix1 = 2*i - 1;
        pix2 = 2*i;
        pix3 = 2*i + 1;
        pix4 = 2*i + 2;
        

        % ピクセル1
        Y_ch(pix4) = Y1;
        U_ch(pix4) = U0;
        V_ch(pix4) = V0;
        
        % ピクセル2
        Y_ch(pix3) = Y0;
        U_ch(pix3) = U0;  % 同じU0
        V_ch(pix3) = V0;  % 同じV0


                % ピクセル3
        Y_ch(pix2) = Y3;
        U_ch(pix2) = U1;  % 同じU0
        V_ch(pix2) = V1;  % 同じV0

             % ピクセル4
        Y_ch(pix1) = Y2;
        U_ch(pix1) = U1;  % 同じU0
        V_ch(pix1) = V1;  % 同じV0

    end

    %==============================
    % 7) YUVを [高さ=240, 幅=320] に reshape
    %==============================
    % [320,240] (幅,高さ) → 転置 → (240×320) 行列
    if length(Y_ch) ~= 76800
        error('画素数が一致しません。(想定:76800, 実際:%d)', length(Y_ch));
    end
    Ymat = reshape(Y_ch, [320, 240])';  % (240 x 320)
    Umat = reshape(U_ch, [320, 240])';
    Vmat = reshape(V_ch, [320, 240])';
    
    %==============================
    % 8) YUV → RGBへ変換
    %==============================
    % ここでは簡単な BT.601 変換を例示 (Y[16..235],U[16..240],V[16..240]想定)
    % YUV値が0-255で取れているとして、(実際にはフォーマットにより範囲が異なる場合あり)
    % R = 1.164*(Y-16) + 1.596*(V-128)
    % G = 1.164*(Y-16) - 0.813*(V-128) - 0.391*(U-128)
    % B = 1.164*(Y-16) + 2.018*(U-128)
    
    Yf = double(Ymat);
    Uf = double(Umat);
    Vf = double(Vmat);
    
    Rf = 1.164 .* (Yf - 16) + 1.596 .* (Vf - 128);
    Gf = 1.164 .* (Yf - 16) + 0.813 .* (Vf - 128) + 0.391 .* (Uf - 128);
    Bf = 1.164 .* (Yf - 16) + 2.018 .* (Uf - 128);
    
    % 範囲 [0..255] にクリップして uint8 化
    R = uint8(max(min(Rf,255),0));
    G = uint8(max(min(Gf,255),0));
    B = uint8(max(min(Bf,255),0));
    
    imgRGB = cat(3, R, G, B);
    
    %==============================
    % 9) 表示
    %==============================
    figure;
    imshow(imgRGB);
    size(imgRGB)
    title('QVGA (YUV422 -> RGB)');
end

close all;
file = "./teraterm.log";
viewQVGAfromYUV(file);