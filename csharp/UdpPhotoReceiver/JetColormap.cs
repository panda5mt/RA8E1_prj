using System.Drawing;

namespace UdpPhotoReceiver;

public static class JetColormap
{
    public static byte[] CreateBgrLut256()
    {
        // Jet-like colormap (close to MATLAB's jet), 256 entries, packed as B,G,R bytes.
        var lut = new byte[256 * 3];

        for (int i = 0; i < 256; i++)
        {
            double v = i / 255.0;

            double r = Clamp01(1.5 - Math.Abs(4.0 * v - 3.0));
            double g = Clamp01(1.5 - Math.Abs(4.0 * v - 2.0));
            double b = Clamp01(1.5 - Math.Abs(4.0 * v - 1.0));

            int baseIdx = i * 3;
            lut[baseIdx + 0] = (byte)Math.Round(b * 255.0);
            lut[baseIdx + 1] = (byte)Math.Round(g * 255.0);
            lut[baseIdx + 2] = (byte)Math.Round(r * 255.0);
        }

        return lut;
    }

    private static double Clamp01(double x) => x < 0 ? 0 : x > 1 ? 1 : x;
}
