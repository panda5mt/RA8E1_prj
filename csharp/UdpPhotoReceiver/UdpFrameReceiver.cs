using System.Buffers.Binary;
using System.Diagnostics;
using System.Net.Sockets;

namespace UdpPhotoReceiver;

public sealed class UdpFrameReceiver : IDisposable
{
    private const uint MagicNumber = 0x12345678;
    private const int HeaderSizeBytes = 24;

    private readonly int _localPort;
    private readonly Action<ReadOnlyMemory<byte>> _onFrame;
    private readonly TimeSpan _frameTimeout;

    private UdpClient? _udp;
    private readonly FrameAssembler _assembler = new();

    public UdpFrameReceiver(int localPort, Action<ReadOnlyMemory<byte>> onFrame, TimeSpan frameTimeout)
    {
        _localPort = localPort;
        _onFrame = onFrame;
        _frameTimeout = frameTimeout;
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        _udp = new UdpClient(_localPort);
        _udp.Client.ReceiveBufferSize = 4 * 1024 * 1024;

        while (!cancellationToken.IsCancellationRequested)
        {
            if (_assembler.InProgress && _assembler.Elapsed > _frameTimeout)
            {
                if (_assembler.HasAnyChunk)
                {
                    _onFrame(_assembler.ReconstructFrame());
                }
                _assembler.Reset();
            }

            UdpReceiveResult result = await _udp.ReceiveAsync(cancellationToken).ConfigureAwait(false);
            ProcessDatagram(result.Buffer);
        }
    }

    private void ProcessDatagram(byte[] datagram)
    {
        if (datagram.Length < HeaderSizeBytes)
        {
            return;
        }

        ReadOnlySpan<byte> span = datagram;
        uint magic = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(0, 4));
        if (magic != MagicNumber)
        {
            return;
        }

        uint totalSize = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(4, 4));
        uint chunkIndex = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(8, 4));
        uint totalChunks = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(12, 4));
        _ = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(16, 4)); // chunk_offset (unused)
        ushort chunkDataSize = BinaryPrimitives.ReadUInt16LittleEndian(span.Slice(20, 2));
        _ = BinaryPrimitives.ReadUInt16LittleEndian(span.Slice(22, 2)); // checksum (unused)

        if (totalChunks == 0 || totalSize == 0)
        {
            return;
        }

        int payloadAvailable = datagram.Length - HeaderSizeBytes;
        int actualSize = Math.Min((int)chunkDataSize, payloadAvailable);
        if (actualSize <= 0)
        {
            return;
        }

        var payload = datagram.AsMemory(HeaderSizeBytes, actualSize);

        if (chunkIndex == 0)
        {
            if (_assembler.InProgress && _assembler.HasAnyChunk)
            {
                _onFrame(_assembler.ReconstructFrame());
            }
            _assembler.Reset();

            _assembler.StartNew(totalChunks: (int)totalChunks, totalSize: (int)totalSize);
        }

        if (!_assembler.InProgress)
        {
            return;
        }

        _assembler.AddChunk((int)chunkIndex, payload);

        if (_assembler.IsComplete)
        {
            _onFrame(_assembler.ReconstructFrame());
            _assembler.Reset();
        }
    }

    public void Dispose()
    {
        _udp?.Dispose();
        _udp = null;
    }
}
