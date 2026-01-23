using System.Diagnostics;
using System.Windows.Forms;

namespace UdpPhotoReceiver;

public partial class Form1 : Form
{
    private readonly CancellationTokenSource _cts = new();
    private readonly UdpFrameReceiver _receiver;
    private readonly object _renderLock = new();
    private DepthRenderer _renderer;
    private readonly Stopwatch _stopwatch = Stopwatch.StartNew();
    private long _framesDisplayed;
    private long _lastStatsMs;
    private volatile RenderMode _mode = RenderMode.Heatmap;

    private volatile int _heatmapMin = 150;
    private volatile int _heatmapMax = 255;

    public Form1()
    {
        InitializeComponent();

        pictureBox.SizeMode = PictureBoxSizeMode.Zoom;

        toolStripComboMode.Items.Add("Heatmap");
        toolStripComboMode.Items.Add("Grayscale");
        toolStripComboMode.SelectedIndex = 0;
        toolStripComboMode.SelectedIndexChanged += (_, _) =>
        {
            _mode = toolStripComboMode.SelectedIndex == 1 ? RenderMode.Grayscale : RenderMode.Heatmap;
        };

        // Heatmap range UI wiring.
        numericHeatMin.ValueChanged += (_, _) =>
        {
            int v = (int)numericHeatMin.Value;
            int hi = (int)numericHeatMax.Value;
            if (v >= hi)
            {
                numericHeatMax.Value = Math.Min(255, v + 1);
                hi = (int)numericHeatMax.Value;
            }
            _heatmapMin = v;
            _heatmapMax = hi;
        };
        numericHeatMax.ValueChanged += (_, _) =>
        {
            int v = (int)numericHeatMax.Value;
            int lo = (int)numericHeatMin.Value;
            if (v <= lo)
            {
                numericHeatMin.Value = Math.Max(0, v - 1);
                lo = (int)numericHeatMin.Value;
            }
            _heatmapMin = lo;
            _heatmapMax = v;
        };

        // Initialize backing fields from designer defaults.
        _heatmapMin = (int)numericHeatMin.Value;
        _heatmapMax = (int)numericHeatMax.Value;

        _renderer = new DepthRenderer(width: 320, height: 240);
        _receiver = new UdpFrameReceiver(
            localPort: 9000,
            onFrame: OnFrame,
            frameTimeout: TimeSpan.FromSeconds(10));

        Shown += (_, _) => Start();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _cts.Cancel();
        _receiver.Dispose();
        lock (_renderLock)
        {
            _renderer.Dispose();
        }
        base.OnFormClosing(e);
    }

    private void Start()
    {
        toolStripStatusLabel.Text = "Listening on UDP port 9000";

        _ = Task.Run(async () =>
        {
            try
            {
                await _receiver.RunAsync(_cts.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // expected on shutdown
            }
            catch (Exception ex)
            {
                BeginInvoke(() => toolStripStatusLabel.Text = $"Receiver error: {ex.Message}");
            }
        }, _cts.Token);
    }

    private void OnFrame(ReadOnlyMemory<byte> frameData)
    {
        try
        {
            byte rangeMin = (byte)Math.Clamp(_heatmapMin, 0, 255);
            byte rangeMax = (byte)Math.Clamp(_heatmapMax, 0, 255);

            (int w, int h) = InferFrameDimsFromTotalSize(frameData.Length, defaultW: 320, defaultH: 240);

            lock (_renderLock)
            {
                if (_renderer.Width != w || _renderer.Height != h)
                {
                    _renderer.Dispose();
                    _renderer = new DepthRenderer(width: w, height: h);
                }

                _renderer.RenderIntoBitmap(frameData.Span, _mode, rangeMin, rangeMax);
            }

            BeginInvoke(() =>
            {
                lock (_renderLock)
                {
                    pictureBox.Image = _renderer.Bitmap;
                }
                _framesDisplayed++;

                var nowMs = _stopwatch.ElapsedMilliseconds;
                if (nowMs - _lastStatsMs >= 1000)
                {
                    var fps = _framesDisplayed / Math.Max(1e-6, _stopwatch.Elapsed.TotalSeconds);
                    toolStripStatusLabel.Text = $"Frames: {_framesDisplayed} ({fps:F2} fps)";
                    _lastStatsMs = nowMs;
                }
            });
        }
        catch
        {
            // keep receiving even if a bad frame arrives
        }
    }

    private static (int width, int height) InferFrameDimsFromTotalSize(int totalSize, int defaultW, int defaultH)
    {
        if (totalSize <= 0)
        {
            return (defaultW, defaultH);
        }

        // Known cases used in this repo/tooling.
        if (totalSize == 320 * 240)
        {
            return (320, 240);
        }
        if (totalSize == 256 * 128)
        {
            return (256, 128);
        }
        if (totalSize == 128 * 128)
        {
            return (128, 128);
        }

        // Fallback: try common widths.
        if (totalSize % 320 == 0)
        {
            int h = totalSize / 320;
            if (h >= 1 && h <= 240)
            {
                return (320, h);
            }
        }
        if (totalSize % 256 == 0)
        {
            int h = totalSize / 256;
            if (h >= 1 && h <= 240)
            {
                return (256, h);
            }
        }
        if (totalSize % 128 == 0)
        {
            int h = totalSize / 128;
            if (h >= 1 && h <= 240)
            {
                return (128, h);
            }
        }

        return (defaultW, defaultH);
    }
}
