using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace ChairOverlay;

public partial class MainWindow : Window
{
    private PipeClient? _pipeClient;
    private CancellationTokenSource? _cts;
    private DispatcherTimer? _topmostTimer;
    private DispatcherTimer? _decayTimer;

    private const int GWL_EXSTYLE = -20;
    private const int WS_EX_TRANSPARENT = 0x00000020;
    private const int WS_EX_TOOLWINDOW  = 0x00000080;

    private static readonly IntPtr HWND_TOPMOST = new(-1);
    private const uint SWP_NOMOVE     = 0x0002;
    private const uint SWP_NOSIZE     = 0x0001;
    private const uint SWP_NOACTIVATE = 0x0010;

    private const int HOTKEY_ID = 9001;
    private const uint MOD_CTRL  = 0x0002;
    private const uint MOD_SHIFT = 0x0004;
    private const uint VK_Q      = 0x51;

    [DllImport("user32.dll")]
    private static extern int GetWindowLong(IntPtr hwnd, int index);
    [DllImport("user32.dll")]
    private static extern int SetWindowLong(IntPtr hwnd, int index, int newStyle);
    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr hwndInsertAfter,
        int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")]
    private static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint vk);
    [DllImport("user32.dll")]
    private static extern bool UnregisterHotKey(IntPtr hwnd, int id);

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Closed += OnClosed;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        var hwnd = new WindowInteropHelper(this).Handle;

        int style = GetWindowLong(hwnd, GWL_EXSTYLE);
        SetWindowLong(hwnd, GWL_EXSTYLE, style | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
        ForceTopmost(hwnd);

        _topmostTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _topmostTimer.Tick += (_, _) => ForceTopmost(hwnd);
        _topmostTimer.Start();

        _decayTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
        _decayTimer.Tick += (_, _) => PolarRing.Tick();
        _decayTimer.Start();

        RegisterHotKey(hwnd, HOTKEY_ID, MOD_CTRL | MOD_SHIFT, VK_Q);
        var source = HwndSource.FromHwnd(hwnd);
        source?.AddHook(WndProc);

        StatusDot.Opacity = 1.0;
        StartPipeListener();
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        const int WM_HOTKEY = 0x0312;
        if (msg == WM_HOTKEY && wParam.ToInt32() == HOTKEY_ID)
        {
            handled = true;
            Close();
        }
        return IntPtr.Zero;
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        var hwnd = new WindowInteropHelper(this).Handle;
        UnregisterHotKey(hwnd, HOTKEY_ID);
        _topmostTimer?.Stop();
        _decayTimer?.Stop();
        _cts?.Cancel();
        _pipeClient?.Dispose();
    }

    private static void ForceTopmost(IntPtr hwnd)
    {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    private void StartPipeListener()
    {
        _cts = new CancellationTokenSource();
        _pipeClient = new PipeClient();
        _pipeClient.RunAsync(_cts.Token);

        Task.Run(async () =>
        {
            var reader = _pipeClient.Readings;
            while (await reader.WaitToReadAsync(_cts.Token))
            {
                while (reader.TryRead(out var r))
                {
                    Dispatcher.Invoke(() =>
                    {
                        StatusDot.Fill = new System.Windows.Media.SolidColorBrush(
                            System.Windows.Media.Color.FromArgb(180, 0, 200, 0));
                        PolarRing.Push(r.Angle, r.EnergyDb, r.Confidence);
                    });
                }
            }
        }, _cts.Token);
    }
}
