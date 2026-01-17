function [pred_label, scores] = lda_predict_from_Wb(features, W, b)
% LDA inference using explicit W,b.
% scores = W' * features + b
%
% Input:
%   features [num_features x 1] or [1 x num_features]
%   W        [num_features x num_classes]
%   b        [num_classes x 1]
%
% Output:
%   pred_label 0-based label
%   scores     [num_classes x 1]

if isrow(features)
    features = features(:);
end

scores = (W.' * features) + b(:);
[~, idx] = max(scores);
pred_label = idx - 1;
end
