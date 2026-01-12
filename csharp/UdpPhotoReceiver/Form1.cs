using System.Diagnostics;

namespace UdpPhotoReceiver;

public partial class Form1 : Form
{
    private readonly CancellationTokenSource _cts = new();
    private readonly UdpFrameReceiver _receiver;
    private readonly DepthRenderer _renderer;
    private readonly Stopwatch _stopwatch = Stopwatch.StartNew();
    private long _framesDisplayed;
    private long _lastStatsMs;
    private volatile RenderMode _mode = RenderMode.Heatmap;

    public Form1()
    {
        InitializeComponent();

        toolStripComboMode.Items.Add("Heatmap");
        toolStripComboMode.Items.Add("Grayscale");
        toolStripComboMode.SelectedIndex = 0;
        toolStripComboMode.SelectedIndexChanged += (_, _) =>
        {
            _mode = toolStripComboMode.SelectedIndex == 1 ? RenderMode.Grayscale : RenderMode.Heatmap;
        };

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
        _renderer.Dispose();
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
            _renderer.RenderIntoBitmap(frameData.Span, _mode);

            BeginInvoke(() =>
            {
                pictureBox.Image = _renderer.Bitmap;
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
}
