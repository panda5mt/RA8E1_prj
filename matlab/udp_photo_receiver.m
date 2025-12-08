function udp_photo_receiver()
    % UDP動画データ受信・リアルタイム表示（マルチフレーム対応）
    close all;
    % UDP設定
    udp_port = 9000;
    
    try
        % UDP受信オブジェクト作成(DSP System Toolbox使用)
        udp_obj = dsp.UDPReceiver( ...
            'LocalIPPort', udp_port, ...
            'MessageDataType', 'uint8', ...
            'MaximumMessageLength', 1024);  % ヘッダー24 + データ512 + マージン
        
        % UDP受信セットアップ
        setup(udp_obj);
        
        fprintf('UDP video receiver started on port %d\n', udp_port);
        fprintf('Waiting for video stream...\n');
        
        % 画像表示ウィンドウ準備
        fig = figure('Name', 'Real-time Video Stream (RA8E1)', 'NumberTitle', 'off');
        ax = axes('Parent', fig);
        img_handle = [];
        
        % 動画受信・表示ループ
        receive_video_stream(udp_obj, ax);
        
        % UDP受信終了
        release(udp_obj);
        
    catch ME
        fprintf('Error: %s\n', ME.message);
        if exist('udp_obj', 'var')
            release(udp_obj);
        end
    end
end

function receive_video_stream(udp_obj, ax)
    % UDPパケットを受信してリアルタイム動画表示
    
    fprintf('Starting video stream reception...\n');
    
    % ヘッダー構造体定義 (C構造体に対応)
    header_size = 24; % uint32*5 + uint16*2 = 24 bytes
    
    % フレーム管理変数
    current_frame_num = 0;
    packets = {};
    total_chunks = 0;
    total_size = 0;
    
    % 画像ハンドル管理（グローバルに管理）
    img_handle = [];
    
    % タイムアウト・統計設定
    frame_start_time = tic;
    frame_timeout_sec = 10;     % フレームタイムアウト10秒
    total_timeout_sec = 600;    % 全体タイムアウト10分
    total_start_time = tic;
    
    % 統計情報
    frames_received = 0;
    frames_displayed = 0;
    last_stats_time = tic;
    
    while toc(total_start_time) < total_timeout_sec
        try
            % パケット受信チェック
            data = udp_obj();
            if ~isempty(data)
                if length(data) >= header_size
                    % ヘッダー解析
                    header = parse_header(data(1:header_size));
                    
                    % マジックナンバーチェック
                    if header.magic_number == uint32(hex2dec('12345678'))
                        chunk_data = data(header_size+1:end);
                        
                        % 新しいフレーム開始チェック
                        if header.chunk_index == 0
                            % 前のフレームが残っていれば処理
                            if ~isempty(packets) && check_frame_complete(packets)
                                img_handle = process_complete_frame(packets, total_chunks, total_size, ax, img_handle);
                                frames_displayed = frames_displayed + 1;
                            end
                            
                            % 新フレーム初期化
                            current_frame_num = current_frame_num + 1;
                            total_chunks = double(header.total_chunks);
                            total_size = double(header.total_size);
                            packets = cell(total_chunks, 1);
                            frame_start_time = tic;
                            
                            % ログ出力を大幅に削減（50フレームごと）
                            if mod(current_frame_num, 50) == 1
                                fprintf('Frame %d started\n', current_frame_num);
                            end
                        end
                        
                        % パケット保存
                        chunk_idx = double(header.chunk_index) + 1;  % MATLABは1-indexed
                        if chunk_idx <= length(packets) && chunk_idx > 0
                            packets{chunk_idx} = chunk_data(1:double(header.chunk_data_size));
                        end
                        
                        % 高速フレーム完了チェック（最後のチャンクで判定）
                        if chunk_idx == total_chunks
                            img_handle = process_complete_frame(packets, total_chunks, total_size, ax, img_handle);
                            frames_received = frames_received + 1;
                            frames_displayed = frames_displayed + 1;
                            
                            % 次フレーム準備
                            packets = {};
                            frame_start_time = tic;
                        end
                        
                    else
                        % 無効なマジックナンバーは無視（デバッグ出力は少なめに）
                    end
                end
            end
            
            % フレームタイムアウトチェック
            if ~isempty(packets) && toc(frame_start_time) > frame_timeout_sec
                fprintf('Frame timeout after %.1f seconds\n', toc(frame_start_time));
                packets = {};  % フレームを破棄
                frame_start_time = tic;
            end
            
            % 統計表示（30秒ごとに削減）
            if toc(last_stats_time) > 30
                fprintf('Stats: %d frames (%.1f fps)\n', ...
                        frames_displayed, frames_displayed/toc(total_start_time));
                last_stats_time = tic;
            end
            
        catch ME
            fprintf('Error in packet processing: %s\n', ME.message);
            % エラーが発生してもループは継続
        end
        
        % 待機時間を最小に（パフォーマンス優先）
        % pause(0.001);  % 削除してCPU使用率向上
        
        % ウィンドウが閉じられたら終了
        if ~isvalid(ax)
            fprintf('Display window closed. Stopping reception.\n');
            break;
        end
    end
    
    fprintf('Video reception ended. Total frames: %d\n', frames_displayed);
end

function is_complete = check_frame_complete(packets)
    % フレーム完了チェック
    if isempty(packets)
        is_complete = false;
        return;
    end
    
    % 全パケットが受信済みかチェック
    is_complete = true;
    for i = 1:length(packets)
        if isempty(packets{i})
            is_complete = false;
            break;
        end
    end
end

function img_handle = process_complete_frame(packets, total_chunks, total_size, ax, img_handle)
    % 完了したフレームを処理・表示（高速化版）
    
    % エラーハンドリングを簡素化
    % フレームデータ復元（高速版）
    frame_data = reconstruct_frame_fast(packets, total_chunks, total_size);
    
    if ~isempty(frame_data)
        % YUV422をRGBに変換（高速版）
        rgb_image = yuv422_to_rgb_fast(frame_data, 320, 240);
        
        % 画像表示更新（最適化）
        if isempty(img_handle) || ~isvalid(img_handle)
            img_handle = imshow(rgb_image, 'Parent', ax);
            title(ax, 'Real-time Video Stream (QVGA YUV422)');
        else
            set(img_handle, 'CData', rgb_image);
        end
        drawnow;  % limitrateを削除して高速化
    end
end

function frame_data = reconstruct_frame_fast(packets, total_chunks, total_size)
    % 高速フレームデータ復元（エラーチェック最小化）
    
    frame_data = zeros(total_size, 1, 'uint8');
    
    for i = 1:total_chunks
        if ~isempty(packets{i})
            chunk_data = packets{i};
            start_pos = (i-1) * 512 + 1;
            end_pos = start_pos + length(chunk_data) - 1;
            if end_pos <= total_size
                frame_data(start_pos:end_pos) = chunk_data;
            end
        end
    end
end

function header = parse_header(header_bytes)
    % バイナリヘッダーを解析
    
    % uint8配列であることを確認
    header_bytes = uint8(header_bytes);
    
    % リトルエンディアンでuint32とuint16を読み取り
    header.magic_number = typecast(header_bytes(1:4), 'uint32');
    header.total_size = typecast(header_bytes(5:8), 'uint32');
    header.chunk_index = typecast(header_bytes(9:12), 'uint32');
    header.total_chunks = typecast(header_bytes(13:16), 'uint32');
    header.chunk_offset = typecast(header_bytes(17:20), 'uint32');
    header.chunk_data_size = typecast(header_bytes(21:22), 'uint16');
    header.checksum = typecast(header_bytes(23:24), 'uint16');
end

function is_valid = verify_checksum(header_bytes)
    % ヘッダーチェックサム検証
    
    % チェックサムフィールドを除いてチェックサム計算
    data_for_checksum = header_bytes(1:22); % 最後の2バイト(checksum)を除く
    
    % uint16配列に変換
    if mod(length(data_for_checksum), 2) ~= 0
        data_for_checksum = [data_for_checksum; 0]; % パディング
    end
    
    uint16_data = typecast(data_for_checksum, 'uint16');
    
    % インターネットチェックサム計算
    sum_val = uint32(sum(double(uint16_data)));
    
    % キャリーを加算
    while bitshift(sum_val, -16) > 0
        sum_val = bitand(sum_val, uint32(hex2dec('FFFF'))) + bitshift(sum_val, -16);
    end
    
    % 1の補数
    calculated_checksum = uint16(bitxor(uint16(sum_val), uint16(hex2dec('FFFF'))));
    
    % 受信したチェックサムと比較
    received_checksum = typecast(header_bytes(23:24), 'uint16');
    is_valid = (calculated_checksum == received_checksum);
end

function rgb_image = yuv422_to_rgb_fast(yuv_data, width, height)
    % 高速YUV422→RGB変換（ベクトル化処理）
    
    expected_bytes = width * height * 2;
    if length(yuv_data) < expected_bytes
        error('YUV data is too short. Expected: %d, Got: %d', expected_bytes, length(yuv_data));
    end
    
    yuv_data = yuv_data(1:expected_bytes);
    
    % ベクトル化処理で高速化
    % 8バイトずつのインデックス作成
    indices = 1:8:length(yuv_data);
    n_blocks = length(indices);
    
    % メモリ事前確保
    Y_ch = zeros(width * height, 1, 'uint8');
    U_ch = zeros(width * height, 1, 'uint8');
    V_ch = zeros(width * height, 1, 'uint8');
    
    % ベクトル化処理
    for k = 1:n_blocks
        i = indices(k);
        if i + 7 <= length(yuv_data)
            % 4ピクセル分のデータ抽出
            block = yuv_data(i:i+7);
            
            % ピクセルベース計算
            pix_base = (k-1) * 4;
            if pix_base + 4 <= width * height
                % 高速代入
                Y_ch(pix_base + 1) = block(8); % Y2
                U_ch(pix_base + 1) = block(7); % U1
                V_ch(pix_base + 1) = block(5); % V1
                
                Y_ch(pix_base + 2) = block(6); % Y3
                U_ch(pix_base + 2) = block(7); % U1
                V_ch(pix_base + 2) = block(5); % V1
                
                Y_ch(pix_base + 3) = block(4); % Y0
                U_ch(pix_base + 3) = block(3); % U0
                V_ch(pix_base + 3) = block(1); % V0
                
                Y_ch(pix_base + 4) = block(2); % Y1
                U_ch(pix_base + 4) = block(3); % U0
                V_ch(pix_base + 4) = block(1); % V0
            end
        end
    end
    
    % reshape処理
    Ymat = reshape(Y_ch, [width, height])';
    Umat = reshape(U_ch, [width, height])';
    Vmat = reshape(V_ch, [width, height])';
    
    % YUVからRGB変換（行列演算で高速化）
    Yf = double(Ymat);
    Uf = double(Umat);
    Vf = double(Vmat);
    
    % ベクトル化RGB変換
    Rf = 1.164 .* (Yf - 16) + 1.596 .* (Vf - 128);
    Gf = 1.164 .* (Yf - 16) - 0.813 .* (Vf - 128) - 0.391 .* (Uf - 128);
    Bf = 1.164 .* (Yf - 16) + 2.018 .* (Uf - 128);
    
    % 範囲クリップ
    R = uint8(max(min(Rf, 255), 0));
    G = uint8(max(min(Gf, 255), 0));
    B = uint8(max(min(Bf, 255), 0));
    
    rgb_image = cat(3, R, G, B);
end

function rgb_image = yuv422_to_rgb(yuv_data, width, height)
    % YUV422(YUYV)をRGBに変換(viewQVGA_YUV.mの処理を参考)
    
    % データが十分な長さか確認
    expected_bytes = width * height * 2;
    if length(yuv_data) < expected_bytes
        error('YUV data is too short. Expected: %d, Got: %d', expected_bytes, length(yuv_data));
    end
    
    % 必要な分だけ切り出し
    yuv_data = yuv_data(1:expected_bytes);
    nPixels = width * height;  % 76800
    
    % YUV チャンネル配列を作成
    Y_ch = zeros(nPixels, 1, 'uint8');
    U_ch = zeros(nPixels, 1, 'uint8');
    V_ch = zeros(nPixels, 1, 'uint8');
    
    % 8バイト(4ピクセル)ずつ処理(リトルエンディアン対応)
    for i = 1:8:length(yuv_data)
        if i+7 > length(yuv_data)
            break;
        end
        
        % 1つ目の4バイト(2ピクセル分)
        % リトルエンディアン: [V0 Y1 U0 Y0]
        Y0 = yuv_data(i+3);   % 4バイト目がY0
        U0 = yuv_data(i+2);   % 3バイト目がU0  
        Y1 = yuv_data(i+1);   % 2バイト目がY1
        V0 = yuv_data(i);     % 1バイト目がV0
        
        % 2つ目の4バイト(2ピクセル分)
        % リトルエンディアン: [V1 Y3 U1 Y2]
        Y2 = yuv_data(i+7);   % 8バイト目がY2
        U1 = yuv_data(i+6);   % 7バイト目がU1
        Y3 = yuv_data(i+5);   % 6バイト目がY3
        V1 = yuv_data(i+4);   % 5バイト目がV1
        
        % ピクセルインデックス(4ピクセル分)
        pix_base = ((i-1)/8) * 4;
        if pix_base + 4 <= nPixels
            % 順序を逆転: 1番→4ピクセル目、2番→3ピクセル目、3番→2ピクセル目、4番→1ピクセル目
            % ピクセル4 (元の2番データ)
            Y_ch(pix_base + 4) = Y1;
            U_ch(pix_base + 4) = U0;
            V_ch(pix_base + 4) = V0;
            
            % ピクセル3 (元の1番データ)  
            Y_ch(pix_base + 3) = Y0;
            U_ch(pix_base + 3) = U0;
            V_ch(pix_base + 3) = V0;
            
            % ピクセル2 (元の3番データ)
            Y_ch(pix_base + 2) = Y3;
            U_ch(pix_base + 2) = U1;
            V_ch(pix_base + 2) = V1;
            
            % ピクセル1 (元の4番データ)
            Y_ch(pix_base + 1) = Y2;
            U_ch(pix_base + 1) = U1;
            V_ch(pix_base + 1) = V1;
        end
    end
    
    % YUVを [高さ×幅] にreshape
    Ymat = reshape(Y_ch, [width, height])';  % (height × width)
    Umat = reshape(U_ch, [width, height])';
    Vmat = reshape(V_ch, [width, height])';
    
    % YUVからRGBへ変換 (BT.601)
    Yf = double(Ymat);
    Uf = double(Umat);
    Vf = double(Vmat);
    
    Rf = 1.164 .* (Yf - 16) + 1.596 .* (Vf - 128);
    Gf = 1.164 .* (Yf - 16) + 0.813 .* (Vf - 128) + 0.391 .* (Uf - 128);
    Bf = 1.164 .* (Yf - 16) + 2.018 .* (Uf - 128);
    
    % 範囲 [0..255] にクリップして uint8 化
    R = uint8(max(min(Rf, 255), 0));
    G = uint8(max(min(Gf, 255), 0));
    B = uint8(max(min(Bf, 255), 0));
    
    % RGB画像を結合
    rgb_image = cat(3, R, G, B);
end