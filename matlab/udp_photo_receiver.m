function udp_photo_receiver()
    % UDP写真データ受信・復元・表示
    close all;
    % UDP設定
    udp_port = 9000;
    
    try
        % UDP受信オブジェクト作成（DSP System Toolbox使用）
        udp_obj = dsp.UDPReceiver( ...
            'LocalIPPort', udp_port, ...
            'MessageDataType', 'uint8', ...
            'MaximumMessageLength', 1024);  % ヘッダー24 + データ512 + マージン
        
        % UDP受信セットアップ
        setup(udp_obj);
        
        fprintf('UDP receiver started on port %d\n', udp_port);
        fprintf('Waiting for photo data packets...\n');
        
        % パケット受信・復元
        photo_data = receive_photo_packets(udp_obj);
        
        if ~isempty(photo_data)
            % YUV422をRGBに変換して表示
            width = 320;
            height = 240;
            rgb_image = yuv422_to_rgb(photo_data, width, height);
            
            % 画像表示
            figure;
            imshow(rgb_image);
            title('Received Photo Data (QVGA YUV422)');
            
            % データ保存
            save_filename = sprintf('received_photo_%s.bin', datestr(now, 'yyyymmdd_HHMMSS'));
            fid = fopen(save_filename, 'wb');
            fwrite(fid, photo_data, 'uint8');
            fclose(fid);
            fprintf('Saved photo data to: %s\n', save_filename);
        else
            fprintf('No complete photo data received.\n');
        end
        
        % UDP受信終了
        release(udp_obj);
        
    catch ME
        fprintf('Error: %s\n', ME.message);
        if exist('udp_obj', 'var')
            release(udp_obj);
        end
    end
end

function photo_data = receive_photo_packets(udp_obj)
    % UDPパケットを受信して写真データを復元
    
    fprintf('Starting packet reception...\n');
    
    % ヘッダー構造体定義 (C構造体に対応)
    header_size = 24; % uint32*5 + uint16*2 = 24 bytes
    
    packets = {};
    total_chunks = 0;
    total_size = 0;
    photo_data = [];
    
    % タイムアウト設定
    start_time = tic;
    timeout_sec = 300; % 300秒タイムアウト
    
    while toc(start_time) < timeout_sec
        try
            % パケット受信チェック
            data = udp_obj();
            if ~isempty(data)
                fprintf('Received packet: %d bytes\n', length(data));
                if length(data) >= header_size
                    % ヘッダー解析
                    fprintf('Parsing header...\n');
                    header = parse_header(data(1:header_size));
                    
                    % マジックナンバーチェック
                    if header.magic_number == uint32(hex2dec('12345678'))
                    % チェックサム検証（一時的に無効化）
                    if true % verify_checksum(data(1:header_size))
                        chunk_data = data(header_size+1:end);
                        
                        % パケット情報表示
                        fprintf('Received chunk %d/%d (size: %d bytes)\n', ...
                                double(header.chunk_index)+1, double(header.total_chunks), ...
                                double(header.chunk_data_size));
                        
                        % 総情報更新（初回のみ）
                        if total_chunks == 0
                            total_chunks = double(header.total_chunks);
                            total_size = double(header.total_size);
                            packets = cell(total_chunks, 1);  % cell配列を初期化
                            fprintf('Expecting %d chunks, total size: %d bytes\n', ...
                                    total_chunks, total_size);
                        end
                        
                        % パケット保存
                        chunk_idx = double(header.chunk_index) + 1;  % MATLABは1-indexed
                        if chunk_idx <= length(packets)
                            packets{chunk_idx} = chunk_data(1:double(header.chunk_data_size));
                        end
                        
                        % 全チャンク受信完了チェック
                        received_count = 0;
                        for k = 1:length(packets)
                            if ~isempty(packets{k})
                                received_count = received_count + 1;
                            end
                        end
                        if received_count == total_chunks
                            fprintf('All chunks received! Reconstructing photo...\n');
                            photo_data = reconstruct_photo(packets, total_chunks, total_size);
                            break;
                        end
                    else
                        fprintf('Checksum error in packet\n');
                    end
                    else
                        fprintf('Invalid magic number: 0x%08X\n', header.magic_number);
                    end
                end
            end
        catch ME
            fprintf('Error in packet processing: %s\n', ME.message);
            fprintf('Error at: %s, line %d\n', ME.stack(1).name, ME.stack(1).line);
            break;
        end
        
        % 短時間待機
        pause(0.01);
    end
    
    if isempty(photo_data)
        received_count = 0;
        for k = 1:length(packets)
            if ~isempty(packets{k})
                received_count = received_count + 1;
            end
        end
        fprintf('Timeout: Received %d/%d chunks\n', received_count, total_chunks);
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

function photo_data = reconstruct_photo(packets, total_chunks, total_size)
    % 受信したパケットから写真データを復元
    
    photo_data = zeros(total_size, 1, 'uint8');
    
    for i = 1:total_chunks
        if ~isempty(packets{i})
            chunk_data = packets{i};
            start_pos = (i-1) * 512 + 1;  % MATLABは1-indexed
            end_pos = min(start_pos + length(chunk_data) - 1, total_size);
            photo_data(start_pos:end_pos) = chunk_data(1:(end_pos-start_pos+1));
        else
            fprintf('Missing chunk %d\n', i-1);  % 0-indexedで表示
        end
    end
    
    fprintf('Photo reconstruction complete: %d bytes\n', length(photo_data));
end

function rgb_image = yuv422_to_rgb(yuv_data, width, height)
    % YUV422(YUYV)をRGBに変換（viewQVGA_YUV.mの処理を参考）
    
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
    
    % 8バイト（4ピクセル）ずつ処理（リトルエンディアン対応）
    for i = 1:8:length(yuv_data)
        if i+7 > length(yuv_data)
            break;
        end
        
        % 1つ目の4バイト（2ピクセル分）
        % リトルエンディアン: [V0 Y1 U0 Y0]
        Y0 = yuv_data(i+3);   % 4バイト目がY0
        U0 = yuv_data(i+2);   % 3バイト目がU0  
        Y1 = yuv_data(i+1);   % 2バイト目がY1
        V0 = yuv_data(i);     % 1バイト目がV0
        
        % 2つ目の4バイト（2ピクセル分）
        % リトルエンディアン: [V1 Y3 U1 Y2]
        Y2 = yuv_data(i+7);   % 8バイト目がY2
        U1 = yuv_data(i+6);   % 7バイト目がU1
        Y3 = yuv_data(i+5);   % 6バイト目がY3
        V1 = yuv_data(i+4);   % 5バイト目がV1
        
        % ピクセルインデックス（4ピクセル分）
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