using System.Drawing;
using System.Drawing.Imaging;

namespace UdpPhotoReceiver;

public sealed class DepthRenderer : IDisposable
{
    private readonly int _width;
    private readonly int _height;
    private readonly byte[] _bgrLut;

    public int Width => _width;
    public int Height => _height;

    public Bitmap Bitmap { get; }

    public DepthRenderer(int width, int height)
    {
        _width = width;
        _height = height;
        _bgrLut = JetColormap.CreateBgrLut256();

        Bitmap = new Bitmap(_width, _height, PixelFormat.Format24bppRgb);
    }

    public void RenderIntoBitmap(ReadOnlySpan<byte> frameData, RenderMode mode, byte rangeMin, byte rangeMax)
    {
        int expected = _width * _height;
        if (frameData.Length < expected)
        {
            return;
        }

        ReadOnlySpan<byte> depth = frameData.Slice(0, expected);

        if (rangeMax <= rangeMin)
        {
            // Avoid divide-by-zero and undefined behavior; treat as identity.
            rangeMin = 0;
            rangeMax = 255;
        }

        Rectangle rect = new Rectangle(0, 0, _width, _height);
        BitmapData bmpData = Bitmap.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);

        try
        {
            unsafe
            {
                byte* dstBase = (byte*)bmpData.Scan0;
                int stride = bmpData.Stride;

                int srcIdx = 0;
                for (int y = 0; y < _height; y++)
                {
                    byte* row = dstBase + y * stride;
                    for (int x = 0; x < _width; x++)
                    {
                        byte d = RemapToByteRange(depth[srcIdx++], rangeMin, rangeMax);
                        int px = x * 3;

                        if (mode == RenderMode.Grayscale)
                        {
                            row[px + 0] = d;
                            row[px + 1] = d;
                            row[px + 2] = d;
                        }
                        else
                        {
                            int lut = d * 3;
                            row[px + 0] = _bgrLut[lut + 0];
                            row[px + 1] = _bgrLut[lut + 1];
                            row[px + 2] = _bgrLut[lut + 2];
                        }
                    }
                }
            }
        }
        finally
        {
            Bitmap.UnlockBits(bmpData);
        }
    }

    private static byte RemapToByteRange(byte value, byte rangeMin, byte rangeMax)
    {
        if (rangeMin == 0 && rangeMax == 255)
        {
            return value;
        }

        if (value <= rangeMin)
        {
            return 0;
        }

        if (value >= rangeMax)
        {
            return 255;
        }

        int v = value;
        int min = rangeMin;
        int max = rangeMax;
        int scaled = (v - min) * 255 / (max - min);
        if (scaled < 0)
        {
            scaled = 0;
        }
        else if (scaled > 255)
        {
            scaled = 255;
        }
        return (byte)scaled;
    }

    public void Dispose()
    {
        Bitmap.Dispose();
    }
}
