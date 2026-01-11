using System.Drawing;
using System.Drawing.Imaging;

namespace UdpPhotoReceiver;

public sealed class DepthRenderer : IDisposable
{
    private readonly int _width;
    private readonly int _height;
    private readonly byte[] _bgrLut;

    public Bitmap Bitmap { get; }

    public DepthRenderer(int width, int height)
    {
        _width = width;
        _height = height;
        _bgrLut = JetColormap.CreateBgrLut256();

        Bitmap = new Bitmap(_width, _height, PixelFormat.Format24bppRgb);
    }

    public void RenderIntoBitmap(ReadOnlySpan<byte> frameData)
    {
        int expected = _width * _height;
        if (frameData.Length < expected)
        {
            return;
        }

        ReadOnlySpan<byte> depth = frameData.Slice(0, expected);

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
                        byte d = depth[srcIdx++];
                        int lut = d * 3;

                        int px = x * 3;
                        row[px + 0] = _bgrLut[lut + 0];
                        row[px + 1] = _bgrLut[lut + 1];
                        row[px + 2] = _bgrLut[lut + 2];
                    }
                }
            }
        }
        finally
        {
            Bitmap.UnlockBits(bmpData);
        }
    }

    public void Dispose()
    {
        Bitmap.Dispose();
    }
}
