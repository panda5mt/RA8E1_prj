function test_udp_connection()
    % UDP接続のテストツール
    % hlac_image_captureで画像が表示されない場合，このスクリプトで診断
    
    close all;
    clc;
    
    fprintf('====================================\n');
    fprintf('UDP接続テストツール\n');
    fprintf('====================================\n\n');
    
    udp_port = 9000;
    test_duration = 10;  % テスト時間(秒)
    
    fprintf('設定:\n');
    fprintf('  UDPポート: %d\n', udp_port);
    fprintf('  テスト時間: %d秒\n\n', test_duration);
    
    try
        % UDP受信オブジェクト作成
        fprintf('UDP受信オブジェクトを作成中...\n');
        udp_obj = dsp.UDPReceiver( ...
            'LocalIPPort', udp_port, ...
            'MessageDataType', 'uint8', ...
            'MaximumMessageLength', 1024);
        
        setup(udp_obj);
        fprintf('✓ UDP受信オブジェクト作成完了\n\n');
        
        fprintf('====================================\n');
        fprintf('パケット受信テスト開始\n');
        fprintf('====================================\n');
        fprintf('RA8E1から画像を送信してください...\n\n');
        
        start_time = tic;
        packet_count = 0;
        total_bytes = 0;
        valid_packets = 0;
        frame_count = 0;
        
        last_frame_num = -1;
        
        while toc(start_time) < test_duration
            % パケット受信
            data = udp_obj();
            
            if ~isempty(data)
                packet_count = packet_count + 1;
                total_bytes = total_bytes + length(data);
                
                % 最初のパケット
                if packet_count == 1
                    fprintf('✓ 最初のパケット受信！\n');
                    fprintf('  サイズ: %d bytes\n', length(data));
                    fprintf('  先頭4バイト: %s\n', ...
                            dec2hex(typecast(data(1:min(4,end)), 'uint8')));
                end
                
                % ヘッダー解析
                if length(data) >= 24
                    magic = typecast(data(1:4), 'uint32');
                    
                    if magic == uint32(hex2dec('12345678'))
                        valid_packets = valid_packets + 1;
                        
                        total_size = typecast(data(5:8), 'uint32');
                        chunk_idx = typecast(data(9:12), 'uint32');
                        total_chunks = typecast(data(13:16), 'uint32');
                        frame_num = typecast(data(17:20), 'uint32');
                        
                        % 新しいフレーム
                        if chunk_idx == 0 && frame_num ~= last_frame_num
                            frame_count = frame_count + 1;
                            last_frame_num = frame_num;
                            
                            fprintf('\nフレーム #%d:\n', frame_num);
                            fprintf('  総サイズ: %d bytes\n', total_size);
                            fprintf('  チャンク数: %d\n', total_chunks);
                            fprintf('  期待パケット数: %d\n', total_chunks);
                        end
                        
                        % 進捗表示(10パケットごと)
                        if mod(valid_packets, 10) == 0
                            fprintf('.');
                        end
                    else
                        fprintf('  警告: 不正なマジックナンバー: 0x%08X\n', magic);
                    end
                else
                    fprintf('  警告: パケットサイズ不足: %d bytes\n', length(data));
                end
            end
            
            pause(0.001);
        end
        
        % 結果表示
        fprintf('\n\n====================================\n');
        fprintf('テスト結果\n');
        fprintf('====================================\n');
        fprintf('テスト時間: %.1f秒\n', toc(start_time));
        fprintf('受信パケット数: %d\n', packet_count);
        fprintf('有効なパケット数: %d\n', valid_packets);
        fprintf('受信フレーム数: %d\n', frame_count);
        fprintf('総受信データ: %.2f KB\n', total_bytes / 1024);
        
        if packet_count > 0
            fprintf('平均パケットサイズ: %.1f bytes\n', total_bytes / packet_count);
            fprintf('平均レート: %.2f KB/s\n', (total_bytes / 1024) / toc(start_time));
        end
        
        fprintf('\n診断結果:\n');
        
        if packet_count == 0
            fprintf('✗ パケットが受信されていません\n');
            fprintf('\n考えられる原因:\n');
            fprintf('  1. RA8E1が画像を送信していない\n');
            fprintf('  2. ネットワーク接続の問題\n');
            fprintf('  3. ファイアウォールがブロックしている\n');
            fprintf('  4. ポート番号が一致していない\n');
            fprintf('\n対策:\n');
            fprintf('  1. RA8E1のプログラムが動作しているか確認\n');
            fprintf('  2. PC のIPアドレスを確認(ipconfig コマンド)\n');
            fprintf('  3. ファイアウォール設定を確認\n');
            fprintf('  4. ポート%dが使用可能か確認\n', udp_port);
        elseif valid_packets == 0
            fprintf('✗ パケットは受信されますが，フォーマットが不正です\n');
            fprintf('\n考えられる原因:\n');
            fprintf('  1. RA8E1のプログラムバージョンが古い\n');
            fprintf('  2. 別のプログラムからのパケット\n');
            fprintf('\n対策:\n');
            fprintf('  1. RA8E1のファームウェアを確認\n');
            fprintf('  2. マジックナンバーを確認\n');
        elseif frame_count == 0
            fprintf('⚠ 有効なパケットはありますが，フレームが完成していません\n');
            fprintf('\n考えられる原因:\n');
            fprintf('  1. パケットロスが多い\n');
            fprintf('  2. ネットワークが不安定\n');
            fprintf('\n対策:\n');
            fprintf('  1. ネットワークケーブルを確認\n');
            fprintf('  2. WiFiの場合は有線接続を試す\n');
            fprintf('  3. テスト時間を長くする\n');
        else
            fprintf('✓ UDP受信は正常に動作しています\n');
            fprintf('✓ フレームも受信できています\n');
            fprintf('\nhlac_image_captureでの問題の可能性:\n');
            fprintf('  1. 画像デコード処理の問題\n');
            fprintf('  2. 表示処理の問題\n');
            fprintf('\n次のステップ:\n');
            fprintf('  >> test_image_decode  (デコードテスト)\n');
        end
        
        fprintf('====================================\n');
        
        % クリーンアップ
        release(udp_obj);
        
    catch ME
        fprintf('\nエラーが発生しました:\n');
        fprintf('  %s\n', ME.message);
        fprintf('  場所: %s (line %d)\n', ME.stack(1).name, ME.stack(1).line);
        
        if contains(ME.message, 'DSP System Toolbox')
            fprintf('\n✗ DSP System Toolboxがインストールされていません\n');
            fprintf('  MATLABのアドオンマネージャーからインストールしてください\n');
        end
        
        if exist('udp_obj', 'var')
            release(udp_obj);
        end
    end
end

function test_image_decode()
    % 画像デコードのテスト
    
    fprintf('====================================\n');
    fprintf('画像デコードテスト\n');
    fprintf('====================================\n\n');
    
    % テスト用のダミーYUYVデータを生成
    width = 320;
    height = 240;
    
    fprintf('テスト画像サイズ: %dx%d\n', width, height);
    fprintf('データサイズ: %d bytes\n\n', width * height * 2);
    
    % グラデーションパターンを生成
    fprintf('1. グラデーションパターンのテスト...\n');
    yuyv_data = generate_test_pattern_gradient(width, height);
    rgb_img = decode_yuyv_test(yuyv_data, width, height);
    
    if ~isempty(rgb_img)
        figure('Name', 'Test 1: Gradient Pattern');
        imshow(rgb_img);
        title('グラデーションパターン');
        fprintf('✓ グラデーションパターンのデコード成功\n\n');
    end
    
    % チェッカーボードパターン
    fprintf('2. チェッカーボードパターンのテスト...\n');
    yuyv_data = generate_test_pattern_checkerboard(width, height);
    rgb_img = decode_yuyv_test(yuyv_data, width, height);
    
    if ~isempty(rgb_img)
        figure('Name', 'Test 2: Checkerboard Pattern');
        imshow(rgb_img);
        title('チェッカーボードパターン');
        fprintf('✓ チェッカーボードパターンのデコード成功\n\n');
    end
    
    fprintf('====================================\n');
    fprintf('デコードテスト完了\n');
    fprintf('====================================\n');
end

function yuyv_data = generate_test_pattern_gradient(width, height)
    % グラデーションパターン生成
    yuyv_data = zeros(width * height * 2, 1, 'uint8');
    
    idx = 1;
    for y = 0:height-1
        Y_val = uint8(y * 255 / height);
        for x = 0:2:width-1
            yuyv_data(idx) = Y_val;
            yuyv_data(idx+1) = 128;  % U
            yuyv_data(idx+2) = Y_val;
            yuyv_data(idx+3) = 128;  % V
            idx = idx + 4;
        end
    end
end

function yuyv_data = generate_test_pattern_checkerboard(width, height)
    % チェッカーボードパターン生成
    yuyv_data = zeros(width * height * 2, 1, 'uint8');
    block_size = 40;
    
    idx = 1;
    for y = 0:height-1
        for x = 0:2:width-1
            bx = floor(x / block_size);
            by = floor(y / block_size);
            if mod(bx + by, 2) == 0
                Y_val = 255;
            else
                Y_val = 0;
            end
            
            yuyv_data(idx) = Y_val;
            yuyv_data(idx+1) = 128;
            yuyv_data(idx+2) = Y_val;
            yuyv_data(idx+3) = 128;
            idx = idx + 4;
        end
    end
end

function rgb_img = decode_yuyv_test(data, width, height)
    % YUYVデコードテスト版
    try
        num_pixels = width * height;
        
        Y = zeros(num_pixels, 1, 'uint8');
        U = zeros(num_pixels, 1, 'uint8');
        V = zeros(num_pixels, 1, 'uint8');
        
        for i = 1:2:num_pixels
            idx = (i-1) * 2 + 1;
            Y(i) = data(idx);
            U(i) = data(idx+1);
            Y(i+1) = data(idx+2);
            V(i+1) = data(idx+3);
            
            if i > 1
                U(i) = U(i-1);
                V(i) = V(i-1);
            end
        end
        
        % YUVからRGB変換
        Y = double(Y);
        U = double(U) - 128;
        V = double(V) - 128;
        
        R = Y + 1.402 * V;
        G = Y - 0.344136 * U - 0.714136 * V;
        B = Y + 1.772 * U;
        
        R = uint8(max(0, min(255, R)));
        G = uint8(max(0, min(255, G)));
        B = uint8(max(0, min(255, B)));
        
        rgb_img = reshape([R, G, B], height, width, 3);
        
    catch ME
        fprintf('✗ デコードエラー: %s\n', ME.message);
        rgb_img = [];
    end
end
