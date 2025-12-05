function test_udp_simple()
    % 簡単なUDP受信テスト
    
    try
        fprintf('Creating UDP receiver...\n');
        
        % dsp.UDPReceiverの作成
        udp_obj = dsp.UDPReceiver();
        udp_obj.LocalIPPort = 9000;
        udp_obj.MessageDataType = 'uint8';
        udp_obj.MaximumMessageLength = 536;
        
        fprintf('UDP receiver created successfully\n');
        fprintf('Waiting for data...\n');
        
        % 10秒間受信を試行
        start_time = tic;
        while toc(start_time) < 10
            data = udp_obj();
            if ~isempty(data)
                fprintf('Received %d bytes: ', length(data));
                if length(data) >= 4
                    fprintf('First 4 bytes: %02X %02X %02X %02X\n', ...
                            data(1), data(2), data(3), data(4));
                else
                    fprintf('Data: ');
                    for i = 1:length(data)
                        fprintf('%02X ', data(i));
                    end
                    fprintf('\n');
                end
            end
            pause(0.1);
        end
        
        fprintf('Test completed\n');
        release(udp_obj);
        
    catch ME
        fprintf('Error: %s\n', ME.message);
        fprintf('Error location: %s\n', ME.stack(1).name);
        if length(ME.stack) > 1
            fprintf('Line: %d\n', ME.stack(1).line);
        end
        if exist('udp_obj', 'var')
            try
                release(udp_obj);
            catch
            end
        end
    end
end