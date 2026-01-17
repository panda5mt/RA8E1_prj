function hlac_image_capture()
    % HLAC学習用画像キャプチャツール
    % RA8E1からUDP経由で画像を受信し、キーボード入力でクラスラベル付き保存
    % 
    % 使い方:
    %   - 数字キー(0-9): 対応するクラスラベルで画像を保存
    %   - 's': 統計情報を表示
    %   - 'q': 終了
    %   - Space: 現在のフレームを表示のみ（保存なし）
    
    close all;
    
    % 設定
    udp_port = 9000;
    save_dir = 'hlac_training_data';  % 保存先ディレクトリ
    rejected_subdir = '_rejected';    % 欠損が多いフレームの退避先（学習には使わない）
    
    % クラスラベル定義（0-9まで使用可能）
    class_names = {'class0', 'class1', 'class2', 'class3', 'class4', ...
                   'class5', 'class6', 'class7', 'class8', 'class9'};
    
    % 保存先ディレクトリ作成
    if ~exist(save_dir, 'dir')
        mkdir(save_dir);
    end
    
    % クラスごとのディレクトリ作成
    for i = 1:length(class_names)
        class_dir = fullfile(save_dir, class_names{i});
        if ~exist(class_dir, 'dir')
            mkdir(class_dir);
        end
    end

    % 欠損が多いフレームの退避用ディレクトリ作成
    rejected_root = fullfile(save_dir, rejected_subdir);
    if ~exist(rejected_root, 'dir')
        mkdir(rejected_root);
    end
    for i = 1:length(class_names)
        rej_class_dir = fullfile(rejected_root, class_names{i});
        if ~exist(rej_class_dir, 'dir')
            mkdir(rej_class_dir);
        end
    end
    
    % 統計情報初期化
    stats = struct();
    for i = 1:length(class_names)
        stats.(class_names{i}) = 0;
    end
    
    try
        % UDP受信オブジェクト作成
        udp_obj = dsp.UDPReceiver( ...
            'LocalIPPort', udp_port, ...
            'MessageDataType', 'uint8', ...
            'MaximumMessageLength', 1024);
        
        setup(udp_obj);
        
        fprintf('====================================\n');
        fprintf('HLAC Image Capture Tool\n');
        fprintf('====================================\n');
        fprintf('UDP受信ポート: %d\n', udp_port);
        fprintf('保存先: %s\n', save_dir);
        fprintf('\n操作方法:\n');
        fprintf('  0-9: クラスラベル付きで画像保存\n');
        fprintf('  s:   統計情報表示\n');
        fprintf('  q:   終了\n');
        fprintf('====================================\n\n');
        
        % 画像表示ウィンドウ準備
        fig = figure('Name', 'HLAC Image Capture', 'NumberTitle', 'off', ...
                     'KeyPressFcn', @(src,evt) set(src, 'UserData', evt.Character));
        ax = axes('Parent', fig);
        
        
        % 受信・キャプチャループ
        [stats, total_saved] = capture_loop(udp_obj, ax, fig, save_dir, class_names, stats, rejected_subdir);
        
        % 終了処理
        release(udp_obj);
        
        % 最終統計表示
        fprintf('\n====================================\n');
        fprintf('キャプチャ完了\n');
        fprintf('====================================\n');
        display_statistics(stats);
        fprintf('総保存枚数: %d\n', total_saved);
        fprintf('====================================\n');
        
    catch ME
        fprintf('エラー: %s\n', ME.message);
        if exist('udp_obj', 'var')
            release(udp_obj);
        end
    end
end

function [stats, total_saved] = capture_loop(udp_obj, ax, fig, save_dir, class_names, stats, rejected_subdir)
    % メインキャプチャループ
    
    header_size = 24;

    % このプロジェクトの送信データ想定（udp_photo_receiver.m と同じ）
    frame_width = 320;
    frame_height = 240;
    
    % フレーム管理変数
    packets = {};
    total_chunks = 0;
    total_size = 0;
    current_frame = [];

    % 保存品質管理
    max_missing_chunks_to_save = 5; % この数以下の欠損なら保存する（表示は常に行う）
    save_rejected_frames = true;    % 欠損が多いフレームは _rejected に退避保存
    current_frame_id = 0;
    last_frame_id = -1;
    last_missing_chunks = inf;
    
    img_handle = [];
    total_saved = 0;
    
    % デバッグ情報
    packet_count = 0;
    last_status_time = tic;
    received_frames = 0;
    
    fprintf('UDPパケット受信待機中...\n');
    
    while ishandle(fig)
        try
            % 高速パケット連続受信（バッファ蓄積対応）
            packets_in_loop = 0;
            while packets_in_loop < 20  % 最大20パケット連続処理
                data = udp_obj();
                
                if isempty(data)
                    break;
                end
                
                packets_in_loop = packets_in_loop + 1;
                packet_count = packet_count + 1;
                
                % 最初のパケット受信時に通知
                if packet_count == 1
                    fprintf('✓ 最初のUDPパケットを受信しました！\n');
                end
                
                % 定期的にステータス表示
                if toc(last_status_time) > 5
                    fprintf('受信状況: パケット=%d, フレーム=%d\n', packet_count, received_frames);
                    last_status_time = tic;
                end
                
                if length(data) >= header_size
                    % ヘッダー解析
                    header_bytes = data(1:header_size);
                    magic_number = typecast(header_bytes(1:4), 'uint32');
                    
                    if magic_number == uint32(hex2dec('12345678'))
                        total_size_val = typecast(header_bytes(5:8), 'uint32');
                        chunk_index_val = typecast(header_bytes(9:12), 'uint32');
                        total_chunks_val = typecast(header_bytes(13:16), 'uint32');
                        chunk_offset_val = typecast(header_bytes(17:20), 'uint32'); %#ok<NASGU>
                        chunk_data_size_val = typecast(header_bytes(21:22), 'uint16');
                        
                        chunk_data = data(header_size+1:end);
                        
                        % 新しいフレーム開始
                        if chunk_index_val == 0
                            current_frame_id = current_frame_id + 1;

                            % 前のフレームを完成させる
                            if ~isempty(packets)
                                [current_frame, last_missing_chunks] = reconstruct_depth_frame(packets, total_chunks, total_size, frame_width, frame_height);
                                last_frame_id = current_frame_id - 1;
                                if ~isempty(current_frame)
                                    img_handle = display_frame(current_frame, ax, img_handle);
                                    received_frames = received_frames + 1;
                                    if received_frames == 1
                                        fprintf('✓ 最初のフレームを表示しました！\n');
                                    end
                                end
                            end
                            
                            % 新フレーム初期化
                            total_chunks = double(total_chunks_val);
                            total_size = double(total_size_val);
                            packets = cell(total_chunks, 1);
                        end
                        
                        % パケット格納
                        chunk_idx = double(chunk_index_val) + 1;
                        if chunk_idx <= total_chunks && chunk_idx > 0
                            actual_size = min(double(chunk_data_size_val), length(chunk_data));
                            if actual_size > 0
                                packets{chunk_idx} = chunk_data(1:actual_size);
                            end
                        end
                        
                        % 最後のチャンクを受信したらフレームを完成させる
                        if chunk_idx == total_chunks && ~isempty(packets)
                            [current_frame, last_missing_chunks] = reconstruct_depth_frame(packets, total_chunks, total_size, frame_width, frame_height);
                            last_frame_id = current_frame_id;
                            if ~isempty(current_frame)
                                img_handle = display_frame(current_frame, ax, img_handle);
                                received_frames = received_frames + 1;
                                if received_frames == 1
                                    fprintf('✓ 最初のフレームを表示しました！\n');
                                end
                            end

                            % 次フレーム準備（udp_photo_receiver.m と同じ考え方）
                            packets = {};
                        end
                    end
                end
            end
            
            % キーボード入力チェック
            if ishandle(fig)
                key = get(fig, 'UserData');
                if ~isempty(key)
                    set(fig, 'UserData', '');  % キーをクリア
                    
                    % キー処理
                    if key == 'q'
                        fprintf('\n終了します...\n');
                        break;
                    elseif key == 's'
                        fprintf('\n---- 統計情報 ----\n');
                        display_statistics(stats);
                        fprintf('総保存枚数: %d\n', total_saved);
                        fprintf('------------------\n\n');
                    elseif key >= '0' && key <= '9'
                        % 画像保存
                        if ~isempty(current_frame)
                            class_idx = str2double(key) + 1;
                            if class_idx <= length(class_names)
                                if last_missing_chunks <= max_missing_chunks_to_save
                                    success = save_image(current_frame, save_dir, class_names{class_idx}, last_frame_id, last_missing_chunks);
                                else
                                    success = false;
                                    fprintf('保存スキップ: 欠損が多いです (missing=%d > %d)\n', last_missing_chunks, max_missing_chunks_to_save);

                                    if save_rejected_frames
                                        rej_success = save_image(current_frame, save_dir, class_names{class_idx}, last_frame_id, last_missing_chunks, rejected_subdir, [class_names{class_idx} '_rej']);
                                        if rej_success
                                            fprintf('退避保存: %s (frame=%d, missing=%d)\n', class_names{class_idx}, last_frame_id, last_missing_chunks);
                                        end
                                    end
                                end
                                if success
                                    stats.(class_names{class_idx}) = stats.(class_names{class_idx}) + 1;
                                    total_saved = total_saved + 1;
                                    fprintf('保存: %s (frame=%d, missing=%d, 総計: %d枚)\n', class_names{class_idx}, last_frame_id, last_missing_chunks, total_saved);
                                end
                            end
                        else
                            fprintf('保存する画像がありません\n');
                        end
                    end
                end
            else
                break;
            end
            
            pause(0.01);  % CPU負荷軽減
            
        catch ME
            fprintf('ループエラー: %s\n', ME.message);
            pause(0.1);
        end
    end
end

function [frame, missing_count] = reconstruct_depth_frame(packets, total_chunks, total_size, width, height)
    % パケットから 8bit depth map (width x height) を復元
    % udp_photo_receiver.m の reconstruct_frame_ultra_fast + extract_depth_map と同等の考え方

    frame = [];
    missing_count = 0;

    try
        total_size = double(total_size);
        if total_size <= 0
            return;
        end

        % 欠損があってもゼロ埋めで復元する（表示・保存を優先）
        frame_data = zeros(total_size, 1, 'uint8');

        for i = 1:total_chunks
            if ~isempty(packets{i})
                chunk_data = packets{i};
                start_pos = (i-1) * 512 + 1;
                end_pos = min(start_pos + length(chunk_data) - 1, total_size);
                if start_pos <= total_size && end_pos >= start_pos
                    frame_data(start_pos:end_pos) = chunk_data(1:(end_pos-start_pos+1));
                end
            else
                missing_count = missing_count + 1;
            end
        end

        expected_pixels = width * height;
        if total_size < expected_pixels
            return;
        end

        depth_raw = frame_data(1:expected_pixels);
        frame = reshape(depth_raw, [width, height])';

    catch ME
        fprintf('フレーム復元エラー: %s\n', ME.message);
    end
end

function img_handle = display_frame(frame, ax, img_handle)
    % フレームを表示
    try
        if isempty(img_handle) || ~ishandle(img_handle)
            img_handle = imshow(frame, [], 'Parent', ax);
            axis(ax, 'image');
            colormap(ax, gray(256));
        else
            set(img_handle, 'CData', frame);
        end
        drawnow limitrate;
    catch
        img_handle = [];
    end
end

function success = save_image(frame, save_dir, class_name, frame_id, missing_chunks, subdir, name_prefix)
    % 画像をクラス別ディレクトリに保存
    success = false;
    
    try
        if nargin < 4
            frame_id = -1;
        end
        if nargin < 5
            missing_chunks = -1;
        end
        if nargin < 6
            subdir = '';
        end
        if nargin < 7
            name_prefix = class_name;
        end

        if isempty(subdir)
            out_dir = fullfile(save_dir, class_name);
        else
            out_dir = fullfile(save_dir, subdir, class_name);
        end
        if ~exist(out_dir, 'dir')
            mkdir(out_dir);
        end

        % ファイル名生成（タイムスタンプ＋品質情報付き）
        timestamp = datestr(now, 'yyyymmdd_HHMMSS_FFF');
        filename = sprintf('%s_%s_f%05d_m%03d.png', name_prefix, timestamp, frame_id, missing_chunks);
        filepath = fullfile(out_dir, filename);
        
        % 保存
        imwrite(frame, filepath);
        success = true;
    catch ME
        fprintf('保存エラー: %s\n', ME.message);
    end
end

function display_statistics(stats)
    % 統計情報表示
    fields = fieldnames(stats);
    for i = 1:length(fields)
        fprintf('  %s: %d枚\n', fields{i}, stats.(fields{i}));
    end
end
