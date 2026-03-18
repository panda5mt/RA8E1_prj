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
%   'compute_softmax_prob'    (default false) % compute best-class probability only when needed
%   'min_best_prob'           (default 0)   % require best softmax prob >= this, else "uncertain"
%   'min_margin'              (default 0)   % require top1-top2 >= this, else show "uncertain"
%   'block_inference'         (default false) % infer per block and overlay class colors
%   'block_rows'              (default 4)   % block grid rows when block_inference=true
%   'block_cols'              (default 4)   % block grid cols when block_inference=true
%   'overlay_alpha'           (default 0.35)% color overlay strength [0..1]
%   'overlay_temporal_smoothing' (default 0) % reserved for compatibility (unused)
%   'block_score_smoothing'   (default 0.85)% temporal smoothing for per-block scores [0..0.99]
%   'show_block_labels'       (default true)% draw class id text on each block
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
p.addParameter('compute_softmax_prob', false);
p.addParameter('min_best_prob', 0);
p.addParameter('min_margin', 0);
p.addParameter('block_inference', false);
p.addParameter('block_rows', 4);
p.addParameter('block_cols', 4);
p.addParameter('overlay_alpha', 0.35);
p.addParameter('overlay_temporal_smoothing', 0);
p.addParameter('block_score_smoothing', 0.85);
p.addParameter('show_block_labels', true);
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
fprintf('Softmax prob: compute=%d, min_best_prob=%.3g\n', opt.compute_softmax_prob, opt.min_best_prob);
fprintf('Block inference: enable=%d, grid=%dx%d, overlay_alpha=%.2f, block_smooth=%.2f\n', ...
    opt.block_inference, opt.block_rows, opt.block_cols, opt.overlay_alpha, ...
    opt.block_score_smoothing);
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
    block_text_handles = gobjects(0);
    block_smoothed_scores = [];
    last_block_display_img = [];
    last_block_title_str = '';

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
        scores = [];

        % Freeze display when too many chunks are missing (avoid flicker from zero-fill).
        show_frame = frame;
        if missing <= opt.max_missing_chunks
            last_good_frame = frame;
        elseif ~isempty(last_good_frame)
            show_frame = last_good_frame;
        end

        display_img = show_frame;

        if do_infer && opt.block_inference
            [display_img, title_str, block_debug] = local_block_infer_overlay(frame, show_frame, cur_frame_id, missing);
            last_block_display_img = display_img;
            last_block_title_str = title_str;
            infer_count = infer_count + 1;
            if opt.debug_print && mod(infer_count, max(1, opt.debug_every)) == 0
                fprintf('infer #%d: frame=%d missing=%d block=%s\n', infer_count, cur_frame_id, missing, block_debug);
            end
        elseif do_infer
            block_smoothed_scores = [];
            local_clear_block_text();
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

            % Optional confidence gates (margin / best softmax probability)
            cname = local_label_to_name(pred, params.class_names);
            margin = local_score_margin(scores);
            need_prob = opt.compute_softmax_prob || (opt.min_best_prob > 0);
            best_prob = NaN;
            if need_prob
                probs = local_softmax(scores);
                best_prob = probs(pred + 1);
            end

            pass_margin = ~(opt.min_margin > 0 && margin < opt.min_margin);
            pass_prob = ~(opt.min_best_prob > 0 && (~isfinite(best_prob) || best_prob < opt.min_best_prob));

            if ~(pass_margin && pass_prob)
                if need_prob
                    title_str = sprintf('frame=%d  missing=%d  pred=uncertain (margin=%.3g prob=%.3f)', cur_frame_id, missing, margin, best_prob);
                else
                    title_str = sprintf('frame=%d  missing=%d  pred=uncertain (margin=%.3g)', cur_frame_id, missing, margin);
                end
                pred_to_print = last_pred_label;
            else
                if need_prob
                    title_str = sprintf('frame=%d  missing=%d  pred=%s (%d)  margin=%.3g prob=%.3f', cur_frame_id, missing, cname, pred, margin, best_prob);
                else
                    title_str = sprintf('frame=%d  missing=%d  pred=%s (%d)  margin=%.3g', cur_frame_id, missing, cname, pred, margin);
                end
                last_pred_label = pred;
                pred_to_print = pred;
            end

            infer_count = infer_count + 1;
            if opt.debug_print && mod(infer_count, max(1, opt.debug_every)) == 0
                if need_prob
                    fprintf('infer #%d: frame=%d missing=%d pred=%d margin=%.3g prob=%.3f scores=[%s]\n', ...
                        infer_count, cur_frame_id, missing, pred_to_print, margin, best_prob, local_scores_to_str(scores));
                else
                    fprintf('infer #%d: frame=%d missing=%d pred=%d margin=%.3g scores=[%s]\n', ...
                        infer_count, cur_frame_id, missing, pred_to_print, margin, local_scores_to_str(scores));
                end
            end

            if opt.debug_feature_stats && mod(infer_count, max(1, opt.debug_every)) == 0
                fprintf('  frame stats: min=%g max=%g mean=%.3g\n', double(min(frame(:))), double(max(frame(:))), mean(double(frame(:))));
                fprintf('  feats stats: min=%.3g max=%.3g mean=%.3g norm2=%.3g\n', min(feats), max(feats), mean(feats), norm(feats));
                fprintf('  lin(W''x)=[%s], b=[%s]\n', local_scores_to_str(lin), local_scores_to_str(params.b(:)));
            end
        else
            scores = [];
            if opt.block_inference && ~isempty(last_block_display_img)
                display_img = last_block_display_img;
                if isempty(last_block_title_str)
                    title_str = sprintf('frame=%d missing=%d block=hold-last', cur_frame_id, missing);
                else
                    title_str = sprintf('%s  [hold]', last_block_title_str);
                end
            else
                block_smoothed_scores = [];
                if missing <= opt.max_missing_chunks
                    title_str = sprintf('frame=%d  missing=%d  (skip infer)', cur_frame_id, missing);
                else
                    title_str = sprintf('frame=%d  missing=%d  (skip infer: missing>%d, show last good)', cur_frame_id, missing, opt.max_missing_chunks);
                end
                local_clear_block_text();
            end
        end

        if isempty(img_handle) || ~ishandle(img_handle)
            if ndims(display_img) == 3
                img_handle = imshow(display_img, 'Parent', ax);
            else
                img_handle = imshow(display_img, [], 'Parent', ax);
                colormap(ax, gray(256));
            end
            axis(ax, 'image');
        else
            set(img_handle, 'CData', display_img);
        end

        title(ax, title_str);
        drawnow limitrate;

        %#ok<NASGU>
        if ~isempty(scores)
            % keep for breakpoint/debug
        end
    end

    function [rgb_img, title_str, debug_str] = local_block_infer_overlay(frame_for_infer, frame_for_show, cur_frame_id, missing)
        rows = max(1, round(opt.block_rows));
        cols = max(1, round(opt.block_cols));
        alpha = max(0, min(1, opt.overlay_alpha));
        block_smooth = max(0, min(0.99, opt.block_score_smoothing));

        [h, w] = size(frame_for_show);
        y_edges = round(linspace(1, h + 1, rows + 1));
        x_edges = round(linspace(1, w + 1, cols + 1));

        gray_norm = double(frame_for_show) / 255.0;
        rgb_img = repmat(gray_norm, [1, 1, 3]);
        class_colors = local_class_colors(size(params.W, 2));

        class_counts = zeros(size(params.W, 2), 1);
        uncertain_count = 0;
        num_used = 0;
        mean_margin = 0;
        mean_prob = 0;

        if opt.show_block_labels
            local_ensure_block_text_handles(rows * cols);
        else
            local_clear_block_text();
        end
        text_idx = 0;

        cls_n = size(params.W, 2);
        block_n = rows * cols;
        if block_smooth > 0
            if isempty(block_smoothed_scores) || size(block_smoothed_scores, 1) ~= block_n || size(block_smoothed_scores, 2) ~= cls_n
                block_smoothed_scores = NaN(block_n, cls_n);
            end
        else
            block_smoothed_scores = [];
        end

        for br = 1:rows
            y1 = y_edges(br);
            y2 = y_edges(br + 1) - 1;
            for bc = 1:cols
                x1 = x_edges(bc);
                x2 = x_edges(bc + 1) - 1;
                if x2 < x1 || y2 < y1
                    continue;
                end

                block = frame_for_infer(y1:y2, x1:x2);
                feats = extract_hlac_features(block, opt.hlac_order, opt.use_sobel);
                feats = feats(:);

                if isfield(params, 'feature_mean') && isfield(params, 'feature_std')
                    mu = params.feature_mean(:);
                    sig = params.feature_std(:);
                    if numel(mu) == numel(feats) && numel(sig) == numel(feats)
                        sig(sig < 1e-12) = 1;
                        feats = (feats - mu) ./ sig;
                    end
                end

                if size(params.W, 1) ~= numel(feats)
                    continue;
                end

                scores_b = (params.W.' * feats) + params.b(:);
                blk_idx = (br - 1) * cols + bc;
                if block_smooth > 0
                    prev_s = block_smoothed_scores(blk_idx, :).';
                    if all(isfinite(prev_s))
                        scores_b = block_smooth * prev_s + (1 - block_smooth) * scores_b;
                    end
                    block_smoothed_scores(blk_idx, :) = scores_b.';
                end

                [pred_b, ~] = local_argmax(scores_b);
                margin_b = local_score_margin(scores_b);
                need_prob_b = opt.compute_softmax_prob || (opt.min_best_prob > 0);
                best_prob_b = NaN;
                if need_prob_b
                    probs_b = local_softmax(scores_b);
                    best_prob_b = probs_b(pred_b + 1);
                end

                pass_margin_b = ~(opt.min_margin > 0 && margin_b < opt.min_margin);
                pass_prob_b = ~(opt.min_best_prob > 0 && (~isfinite(best_prob_b) || best_prob_b < opt.min_best_prob));
                is_uncertain = ~(pass_margin_b && pass_prob_b);

                if is_uncertain
                    c = [0.55, 0.55, 0.55];
                    uncertain_count = uncertain_count + 1;
                    label_text = '?';
                else
                    c = class_colors(pred_b + 1, :);
                    class_counts(pred_b + 1) = class_counts(pred_b + 1) + 1;
                    label_text = sprintf('%d', pred_b);
                end

                num_used = num_used + 1;
                mean_margin = mean_margin + margin_b;
                if need_prob_b && isfinite(best_prob_b)
                    mean_prob = mean_prob + best_prob_b;
                end

                for ch = 1:3
                    region = rgb_img(y1:y2, x1:x2, ch);
                    region = (1 - alpha) * region + alpha * c(ch);
                    rgb_img(y1:y2, x1:x2, ch) = region;
                end

                text_idx = text_idx + 1;
                if opt.show_block_labels
                    cx = (x1 + x2) / 2;
                    cy = (y1 + y2) / 2;
                    set(block_text_handles(text_idx), ...
                        'Position', [cx, cy, 0], 'String', label_text, 'Visible', 'on');
                end
            end
        end

        if opt.show_block_labels && text_idx < numel(block_text_handles)
            for i = text_idx + 1:numel(block_text_handles)
                set(block_text_handles(i), 'Visible', 'off');
            end
        end

        % grid lines
        for i = 2:numel(y_edges)-1
            yy = y_edges(i);
            yy = min(max(1, yy), h);
            rgb_img(yy, :, :) = 1;
        end
        for i = 2:numel(x_edges)-1
            xx = x_edges(i);
            xx = min(max(1, xx), w);
            rgb_img(:, xx, :) = 1;
        end

        if num_used > 0
            mean_margin = mean_margin / num_used;
            if opt.compute_softmax_prob || (opt.min_best_prob > 0)
                mean_prob = mean_prob / num_used;
                title_str = sprintf('frame=%d missing=%d block=%dx%d uncertain=%d margin=%.3g prob=%.3f', ...
                    cur_frame_id, missing, rows, cols, uncertain_count, mean_margin, mean_prob);
            else
                title_str = sprintf('frame=%d missing=%d block=%dx%d uncertain=%d margin=%.3g', ...
                    cur_frame_id, missing, rows, cols, uncertain_count, mean_margin);
            end
        else
            title_str = sprintf('frame=%d missing=%d block=%dx%d (no valid block infer)', cur_frame_id, missing, rows, cols);
        end

        debug_parts = arrayfun(@(v) sprintf('%d', v), class_counts(:).', 'UniformOutput', false);
        debug_str = sprintf('counts=[%s], uncertain=%d', strjoin(debug_parts, ','), uncertain_count);
    end

    function local_ensure_block_text_handles(n)
        if numel(block_text_handles) ~= n || any(~isgraphics(block_text_handles))
            local_clear_block_text();
            block_text_handles = gobjects(n, 1);
            for i = 1:n
                block_text_handles(i) = text(ax, 1, 1, '', ...
                    'Color', 'w', 'FontSize', 8, 'FontWeight', 'bold', ...
                    'HorizontalAlignment', 'center', 'VerticalAlignment', 'middle', ...
                    'Visible', 'off');
            end
        end
    end

    function local_clear_block_text()
        if ~isempty(block_text_handles)
            alive = isgraphics(block_text_handles);
            delete(block_text_handles(alive));
            block_text_handles = gobjects(0);
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

function probs = local_softmax(scores)
% Stable softmax: exp(scores - max(scores)) / sum(exp(...))
scores = scores(:);
mx = max(scores);
e = exp(scores - mx);
den = sum(e);
if den <= 0 || ~isfinite(den)
    probs = zeros(size(scores));
else
    probs = e ./ den;
end
end

function s = local_scores_to_str(scores)
scores = scores(:).';
parts = arrayfun(@(v) sprintf('%.3g', v), scores, 'UniformOutput', false);
s = strjoin(parts, ',');
end

function colors = local_class_colors(class_count)
% Fixed palette (RGB in [0,1]) for clear visual separation.
base = [
    0.90, 0.20, 0.20;  % red
    0.95, 0.65, 0.10;  % orange
    0.95, 0.85, 0.15;  % yellow
    0.25, 0.70, 0.30;  % green
    0.20, 0.75, 0.75;  % cyan
    0.20, 0.45, 0.90;  % blue
    0.55, 0.35, 0.85;  % violet
    0.90, 0.35, 0.70;  % magenta
    0.60, 0.45, 0.30;  % brown
    0.95, 0.95, 0.95   % white
];

if class_count <= size(base, 1)
    colors = base(1:class_count, :);
else
    colors = zeros(class_count, 3);
    for i = 1:class_count
        colors(i, :) = base(mod(i - 1, size(base, 1)) + 1, :);
    end
end
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
