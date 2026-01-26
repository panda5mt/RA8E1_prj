function send_test_frame_256x256(remote_ip, remote_port, num_frames, fps)
%SEND_TEST_FRAME_256X256 Send a synthetic 256x256 uint8 frame over the repo's UDP chunk protocol.
%
% This is a viewer sanity-check tool: it exercises the same header + chunking
% format as the RA8E1 firmware so the C# / MATLAB viewers can be validated
% without hardware.
%
% Usage examples:
%   cd matlab
%   send_test_frame_256x256              % -> 127.0.0.1:9000, 60 frames @ 10 fps
%   send_test_frame_256x256('192.168.0.10', 9000, 120, 30)
%
% Frame format:
%   - payload is raw grayscale (uint8) width*height bytes, row-major
%   - header is 24 bytes little-endian:
%       u32 magic(0x12345678)
%       u32 total_size
%       u32 chunk_index (0-based)
%       u32 total_chunks
%       u32 chunk_offset (unused by viewers; used as frame counter here)
%       u16 chunk_data_size
%       u16 checksum (internet checksum over first 22 header bytes)
%

if nargin < 1 || isempty(remote_ip)
    remote_ip = '127.0.0.1';
end
if nargin < 2 || isempty(remote_port)
    remote_port = 9000;
end
if nargin < 3 || isempty(num_frames)
    num_frames = 60;
end
if nargin < 4 || isempty(fps)
    fps = 10;
end

w = 256;
h = 256;
total_size = w * h;
chunk_payload_max = 512;
total_chunks = ceil(total_size / chunk_payload_max);

fprintf('Sending synthetic %dx%d (%d bytes) to %s:%d\n', w, h, total_size, remote_ip, remote_port);
fprintf('Chunks: %d (payload max %d bytes), frames: %d, fps: %.3g\n', total_chunks, chunk_payload_max, num_frames, fps);

% Sender transport: prefer udpport (base MATLAB), fall back to DSP System Toolbox.
use_udpport = exist('udpport', 'file') == 2;

udp_obj = [];
dsp_sender = [];
try
    if use_udpport
        udp_obj = udpport('datagram', 'IPV4');
    else
        dsp_sender = dsp.UDPSender('RemoteIPAddress', remote_ip, 'RemoteIPPort', remote_port);
        setup(dsp_sender);
    end

    t_frame = 0;
    if fps > 0
        t_frame = 1 / fps;
    end

    magic = uint32(hex2dec('12345678'));

    for frame_id = 1:num_frames
        payload = build_test_payload_u8(w, h, frame_id);

        for chunk_index = 0:(total_chunks - 1)
            byte0 = chunk_index * chunk_payload_max;
            byte1 = min(byte0 + chunk_payload_max, total_size);

            chunk = payload((byte0 + 1):byte1);
            chunk_data_size = numel(chunk);

            header_wo_checksum = [ ...
                u32le(magic); ...
                u32le(uint32(total_size)); ...
                u32le(uint32(chunk_index)); ...
                u32le(uint32(total_chunks)); ...
                u32le(uint32(frame_id)); ...
                u16le(uint16(chunk_data_size)) ...
            ];

            checksum = internet_checksum_u16(header_wo_checksum);
            header = [header_wo_checksum; u16le(checksum)];
            datagram = [header; chunk(:)];

            if use_udpport
                write(udp_obj, datagram, 'uint8', remote_ip, remote_port);
            else
                step(dsp_sender, datagram);
            end
        end

        if t_frame > 0
            pause(t_frame);
        end
    end

    fprintf('Done.\n');

catch ME
    fprintf('Send failed: %s\n', ME.message);

end

try
    if ~isempty(dsp_sender)
        release(dsp_sender);
    end
catch
end

try
    if ~isempty(udp_obj)
        clear udp_obj;
    end
catch
end

end

function payload = build_test_payload_u8(w, h, frame_id)
% Simple diagnostic pattern (row-major): X gradient + moving bar.

[X, Y] = meshgrid(0:(w - 1), 0:(h - 1));
base = uint8(mod(X + 2 * Y, 256));

% moving bright bar
bar_x = mod((frame_id - 1) * 4, w);
bar = uint8(zeros(h, w));
bar(:, max(1, bar_x):min(w, bar_x + 2)) = 255;

img = max(base, bar);

% row-major (same as reshape(depth_raw,[w,h])' in receiver)
payload = reshape(img.', [], 1);
end

function bytes = u32le(v)
% uint32 -> 4 bytes little-endian
v = uint32(v);
bytes = typecast(v, 'uint8');
if ~is_little_endian()
    bytes = flipud(bytes(:));
else
    bytes = bytes(:);
end
end

function bytes = u16le(v)
% uint16 -> 2 bytes little-endian
v = uint16(v);
bytes = typecast(v, 'uint8');
if ~is_little_endian()
    bytes = flipud(bytes(:));
else
    bytes = bytes(:);
end
end

function tf = is_little_endian()
[~, ~, endian] = computer;
tf = (endian == 'L');
end

function checksum = internet_checksum_u16(header_wo_checksum)
% Match matlab/udp_photo_receiver.m verify_checksum() behavior.
% Compute internet checksum over the first 22 bytes.

b = uint8(header_wo_checksum(:));
if mod(numel(b), 2) ~= 0
    b(end + 1) = 0;
end

% Interpret as little-endian uint16 words independent of host endianness.
lo = uint32(b(1:2:end));
hi = uint32(b(2:2:end));
words = lo + bitshift(hi, 8);

sum_val = uint32(sum(double(words)));
while bitshift(sum_val, -16) > 0
    sum_val = bitand(sum_val, uint32(hex2dec('FFFF'))) + bitshift(sum_val, -16);
end

checksum = uint16(bitxor(uint16(sum_val), uint16(hex2dec('FFFF'))));
end
