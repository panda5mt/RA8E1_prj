using System.Diagnostics;

namespace UdpPhotoReceiver;

public sealed class FrameAssembler
{
    private const int ChunkStrideBytes = 512;

    private int _totalChunks;
    private int _totalSize;
    private byte[]?[]? _chunks;
    private bool[]? _received;
    private int _receivedCount;
    private readonly Stopwatch _frameStopwatch = new();

    public bool InProgress => _chunks is not null;

    public bool IsComplete => _chunks is not null && _receivedCount == _totalChunks;

    public bool HasAnyChunk => _chunks is not null && _receivedCount > 0;

    public TimeSpan Elapsed => _frameStopwatch.Elapsed;

    public void StartNew(int totalChunks, int totalSize)
    {
        if (totalChunks <= 0 || totalSize <= 0)
        {
            Reset();
            return;
        }

        _totalChunks = totalChunks;
        _totalSize = totalSize;
        _chunks = new byte[totalChunks][];
        _received = new bool[totalChunks];
        _receivedCount = 0;
        _frameStopwatch.Restart();
    }

    public void AddChunk(int chunkIndex, ReadOnlyMemory<byte> chunkData)
    {
        if (_chunks is null || _received is null)
        {
            return;
        }

        if (chunkIndex < 0 || chunkIndex >= _totalChunks)
        {
            return;
        }

        if (chunkData.Length == 0)
        {
            return;
        }

        if (_received[chunkIndex])
        {
            return;
        }

        _chunks[chunkIndex] = chunkData.ToArray();
        _received[chunkIndex] = true;
        _receivedCount++;
    }

    public ReadOnlyMemory<byte> ReconstructFrame()
    {
        if (_chunks is null)
        {
            return ReadOnlyMemory<byte>.Empty;
        }

        byte[] frame = new byte[_totalSize];

        for (int i = 0; i < _totalChunks; i++)
        {
            byte[]? chunk = _chunks[i];
            if (chunk is null || chunk.Length == 0)
            {
                continue;
            }

            int startPos = i * ChunkStrideBytes;
            if (startPos >= frame.Length)
            {
                continue;
            }

            int copyLen = Math.Min(chunk.Length, frame.Length - startPos);
            Buffer.BlockCopy(chunk, 0, frame, startPos, copyLen);
        }

        return frame;
    }

    public void Reset()
    {
        _chunks = null;
        _totalChunks = 0;
        _totalSize = 0;
        _received = null;
        _receivedCount = 0;
        _frameStopwatch.Reset();
    }
}
