function hlac_udp_inference(varargin)
% Online inference from RA8E1 UDP stream (port 9000 by default).
% Pipeline: depth(uint8) frame -> (optional Sobel) -> HLAC(order=2, 25) -> LDA(W,b)
%
% Requirements:
% - DSP System Toolbox (dsp.UDPReceiver)
% - Trained model in model_dir (default: lda_model/lda_model.mat)
%
% Usage:
%   cd matlab
%   hlac_udp_inference
%
% Options (name,value):
%   'udp_port'               (default 9000)
%   'model_dir'              (default 'lda_model')
%   'frame_width'            (default 320)
%   'frame_height'           (default 240)
%   'max_missing_chunks'     (default 5)  % infer only if missing<=this
%   'infer_on_rejected'      (default false) % if true, infer even when missing>threshold
%   'use_sobel'              (default false) % must match training
%   'hlac_order'             (default 2)    % 1 or 2
%   'score_smoothing'         (default 0)   % 0=no smoothing, 0.8=strong EMA smoothing
%   'min_margin'              (default 0)   % require top1-top2 >= this, else show "uncertain"
%   'debug_print'             (default false)
%   'debug_every'             (default 30)  % print every N inferences
%   'debug_feature_stats'     (default false) % print feature/score decomposition

p = inputParser;
p.addParameter('udp_port', 9000);
p.addParameter('model_dir', 'lda_model');
p.addParameter('frame_width', 320);
p.addParameter('frame_height', 240);
p.addParameter('max_missing_chunks', 5);
p.addParameter('infer_on_rejected', false);
p.addParameter('use_sobel', false);
p.addParameter('hlac_order', 2);
p.addParameter('score_smoothing', 0);
p.addParameter('min_margin', 0);
p.addParameter('debug_print', false);
p.addParameter('debug_every', 30);
p.addParameter('debug_feature_stats', false);
p.parse(varargin{:});
opt = p.Results;

params = load_lda_params(opt.model_dir);

fprintf('====================================\n');
fprintf('HLAC UDP Online Inference\n');
fprintf('====================================\n');
fprintf('UDP port: %d\n', opt.udp_port);
if isfield(params, 'model_dir')
    fprintf('Model dir: %s\n', params.model_dir);
else
    fprintf('Model dir: %s\n', opt.model_dir);
end
fprintf('Classes: %d, Features: %d\n', size(params.W,2), size(params.W,1));
fprintf('Frame: %dx%d\n', opt.frame_width, opt.frame_height);
fprintf('Missing threshold: %d (infer_on_rejected=%d)\n', opt.max_missing_chunks, opt.infer_on_rejected);
fprintf('Preprocess: use_sobel=%d, hlac_order=%d\n', opt.use_sobel, opt.hlac_order);
fprintf('Quit: q\n');
fprintf('====================================\n\n');

udp_obj = [];
try
    udp_obj = dsp.UDPReceiver( ...
        'LocalIPPort', opt.udp_port, ...
        'MessageDataType', 'uint8', ...
        'MaximumMessageLength', 1024);
    setup(udp_obj);

    fig = figure('Name', 'HLAC UDP Inference', 'NumberTitle', 'off', ...
        'KeyPressFcn', @(src,evt) setappdata(src, 'last_key', evt.Character));
    ax = axes('Parent', fig);
    img_handle = [];

    header_size = 24;
    magic = uint32(hex2dec('12345678'));

    packets = {};
    total_chunks = 0;
    total_size = 0;
    frame_id = 0;
    received_mask = [];
    received_count = 0;
    frame_completed = false;
    last_status = tic;
    pkt_count = 0;
    infer_count = 0;
    smoothed_scores = [];
    last_pred_label = 0;

    % Display stabilization: keep last good frame to avoid flicker on packet loss.
    last_good_frame = [];

    while ishandle(fig)
        % Drain a small burst each loop
        for k = 1:50
            data = udp_obj();
            if isempty(data)
                break;
            end
            pkt_count = pkt_count + 1;

            if toc(last_status) > 5
                fprintf('recv: packets=%d, frame_id=%d\n', pkt_count, frame_id);
                last_status = tic;
            end

            if length(data) < header_size
                continue;
            end

            header_bytes = data(1:header_size);
            magic_number = typecast(header_bytes(1:4), 'uint32');
            if magic_number ~= magic
                continue;
            end

            total_size_val = double(typecast(header_bytes(5:8), 'uint32'));
            chunk_index_val = double(typecast(header_bytes(9:12), 'uint32'));
            total_chunks_val = double(typecast(header_bytes(13:16), 'uint32'));
            %#ok<NASGU> chunk_offset_val = typecast(header_bytes(17:20), 'uint32');
            chunk_data_size_val = double(typecast(header_bytes(21:22), 'uint16'));

            chunk_data = data(header_size+1:end);

            if chunk_index_val == 0
                % finalize previous
                if ~isempty(packets) && ~frame_completed
                    [frame, missing] = reconstruct_depth_frame(packets, total_chunks, total_size, opt.frame_width, opt.frame_height);
                    if ~isempty(frame)
                        run_infer_and_show(frame, missing, frame_id);
                    end
                end

                frame_id = frame_id + 1;
                total_chunks = total_chunks_val;
                total_size = total_size_val;
                packets = cell(total_chunks, 1);
                received_mask = false(total_chunks, 1);
                received_count = 0;
                frame_completed = false;
            end

            chunk_idx = chunk_index_val + 1;
            if chunk_idx >= 1 && chunk_idx <= total_chunks
                actual_size = min(chunk_data_size_val, length(chunk_data));
                if actual_size > 0
                    if isempty(packets{chunk_idx})
                        packets{chunk_idx} = chunk_data(1:actual_size);
                        received_mask(chunk_idx) = true;
                        received_count = received_count + 1;
                    end
                end
            end

            if ~frame_completed && ~isempty(packets) && received_count == total_chunks
                [frame, missing] = reconstruct_depth_frame(packets, total_chunks, total_size, opt.frame_width, opt.frame_height);
                if ~isempty(frame)
                    run_infer_and_show(frame, missing, frame_id);
                end
                frame_completed = true;
            end
        end

        % key handling
        key = '';
        if isappdata(fig, 'last_key')
            key = getappdata(fig, 'last_key');
            rmappdata(fig, 'last_key');
        end
        if strcmp(key, 'q')
            break;
        end

        pause(0.01);
    end

catch ME
    fprintf('エラー: %s\n', ME.message);
end

if ~isempty(udp_obj)
    release(udp_obj);
end

    function run_infer_and_show(frame, missing, cur_frame_id)
        do_infer = (missing <= opt.max_missing_chunks) || opt.infer_on_rejected;

        % Freeze display when too many chunks are missing (avoid flicker from zero-fill).
        show_frame = frame;
        if missing <= opt.max_missing_chunks
            last_good_frame = frame;
        elseif ~isempty(last_good_frame)
            show_frame = last_good_frame;
        end

        if do_infer
            feats = extract_hlac_features(frame, opt.hlac_order, opt.use_sobel);
            feats = feats(:);

            % Apply same standardization used during training if available
            if isfield(params, 'feature_mean') && isfield(params, 'feature_std')
                mu = params.feature_mean(:);
                sig = params.feature_std(:);
                if numel(mu) == numel(feats) && numel(sig) == numel(feats)
                    sig(sig < 1e-12) = 1;
                    feats = (feats - mu) ./ sig;
                end
            end

            if size(params.W, 1) ~= numel(feats)
                fprintf('警告: 特徴次元がモデルと一致しません (model=%d, feats=%d)．学習と同じ設定(use_sobel/hlac_order)か確認し，必要なら再学習してください．\n', ...
                    size(params.W, 1), numel(feats));

                title_str = sprintf('frame=%d  missing=%d  (skip infer: dim mismatch model=%d feats=%d)', ...
                    cur_frame_id, missing, size(params.W, 1), numel(feats));

                % Still show the frame
                if isempty(img_handle) || ~ishandle(img_handle)
                    img_handle = imshow(show_frame, [], 'Parent', ax);
                    axis(ax, 'image');
                    colormap(ax, gray(256));
                else
                    set(img_handle, 'CData', show_frame);
                end
                title(ax, title_str);
                drawnow limitrate;
                return;
            end

            lin = (params.W.' * feats);
            scores = lin + params.b(:);
            [pred, ~] = local_argmax(scores);

            % Optional smoothing (EMA) over scores to stabilize online prediction
            alpha = max(0, min(0.99, opt.score_smoothing));
            if alpha > 0
                if isempty(smoothed_scores)
                    smoothed_scores = scores;
                else
                    smoothed_scores = alpha * smoothed_scores + (1 - alpha) * scores;
                end
                [pred_s, scores_s] = local_argmax(smoothed_scores);
                pred = pred_s;
                scores = scores_s;
            end

            % Optional margin gate
            cname = local_label_to_name(pred, params.class_names);
            margin = local_score_margin(scores);
            if opt.min_margin > 0 && margin < opt.min_margin
                title_str = sprintf('frame=%d  missing=%d  pred=uncertain (margin=%.3g)', cur_frame_id, missing, margin);
                pred_to_print = last_pred_label;
            else
                title_str = sprintf('frame=%d  missing=%d  pred=%s (%d)  margin=%.3g', cur_frame_id, missing, cname, pred, margin);
                last_pred_label = pred;
                pred_to_print = pred;
            end

            infer_count = infer_count + 1;
            if opt.debug_print && mod(infer_count, max(1, opt.debug_every)) == 0
                fprintf('infer #%d: frame=%d missing=%d pred=%d margin=%.3g scores=[%s]\n', ...
                    infer_count, cur_frame_id, missing, pred_to_print, margin, local_scores_to_str(scores));
            end

            if opt.debug_feature_stats && mod(infer_count, max(1, opt.debug_every)) == 0
                fprintf('  frame stats: min=%g max=%g mean=%.3g\n', double(min(frame(:))), double(max(frame(:))), mean(double(frame(:))));
                fprintf('  feats stats: min=%.3g max=%.3g mean=%.3g norm2=%.3g\n', min(feats), max(feats), mean(feats), norm(feats));
                fprintf('  lin(W''x)=[%s], b=[%s]\n', local_scores_to_str(lin), local_scores_to_str(params.b(:)));
            end
        else
            scores = [];
            if missing <= opt.max_missing_chunks
                title_str = sprintf('frame=%d  missing=%d  (skip infer)', cur_frame_id, missing);
            else
                title_str = sprintf('frame=%d  missing=%d  (skip infer: missing>%d, show last good)', cur_frame_id, missing, opt.max_missing_chunks);
            end
        end

        if isempty(img_handle) || ~ishandle(img_handle)
            img_handle = imshow(show_frame, [], 'Parent', ax);
            axis(ax, 'image');
            colormap(ax, gray(256));
        else
            set(img_handle, 'CData', show_frame);
        end

        title(ax, title_str);
        drawnow limitrate;

        %#ok<NASGU>
        if ~isempty(scores)
            % keep for breakpoint/debug
        end
    end

end

function name = local_label_to_name(label0, class_names)
if label0 + 1 <= numel(class_names)
    name = class_names{label0+1};
else
    name = sprintf('class%d', label0);
end
end

function margin = local_score_margin(scores)
% top1-top2 margin
scores = scores(:);
if numel(scores) < 2
    margin = Inf;
    return;
end
s = sort(scores, 'descend');
margin = s(1) - s(2);
end

function [pred0, scores_out] = local_argmax(scores)
scores_out = scores(:);
[~, idx] = max(scores_out);
pred0 = idx - 1;
end

function s = local_scores_to_str(scores)
scores = scores(:).';
parts = arrayfun(@(v) sprintf('%.3g', v), scores, 'UniformOutput', false);
s = strjoin(parts, ',');
end

function [frame, missing_count] = reconstruct_depth_frame(packets, total_chunks, total_size, width, height)
% Reconstruct 8-bit depth frame from chunk list (zero-fill missing).

frame = [];
missing_count = 0;

try
    total_size = double(total_size);
    if total_size <= 0
        return;
    end

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

    [width, height] = infer_frame_dims_from_total_size(total_size, width, height);
    expected_pixels = width * height;
    if total_size < expected_pixels
        return;
    end

    depth_raw = frame_data(1:expected_pixels);
    frame = reshape(depth_raw, [width, height])';

catch
    frame = [];
end
end

function [w, h] = infer_frame_dims_from_total_size(total_size, w0, h0)
% Infer (width,height) from total_size when sender uses variable-size payload.

w = w0;
h = h0;
if isempty(w) || isempty(h) || w <= 0 || h <= 0
    w = 320;
    h = 240;
end

if total_size == double(w) * double(h)
    return;
end

if total_size == 320 * 240
    w = 320;
    h = 240;
elseif total_size == 256 * 128
    w = 256;
    h = 128;
elseif total_size == 256 * 256
    w = 256;
    h = 256;
elseif total_size == 128 * 128
    w = 128;
    h = 128;
elseif mod(total_size, 320) == 0
    cand_h = total_size / 320;
    if cand_h >= 1 && cand_h <= 240
        w = 320;
        h = cand_h;
    end
elseif mod(total_size, 256) == 0
    cand_h = total_size / 256;
    if cand_h >= 1 && cand_h <= 512
        w = 256;
        h = cand_h;
    end
end
end
