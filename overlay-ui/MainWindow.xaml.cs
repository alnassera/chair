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
    private SettingsWindow? _settingsWindow;

    private const int GWL_EXSTYLE = -20;
    private const int WS_EX_TRANSPARENT = 0x00000020;
    private const int WS_EX_TOOLWINDOW  = 0x00000080;

    private static readonly IntPtr HWND_TOPMOST = new(-1);
    private const uint SWP_NOMOVE     = 0x0002;
    private const uint SWP_NOSIZE     = 0x0001;
    private const uint SWP_NOACTIVATE = 0x0010;

    private const int HOTKEY_QUIT     = 9001;
    private const int HOTKEY_SETTINGS = 9002;
    private const int HOTKEY_TOGGLE   = 9003;
    private const uint MOD_CTRL  = 0x0002;
    private const uint MOD_SHIFT = 0x0004;
    private const uint VK_Q      = 0x51;
    private const uint VK_S      = 0x53;
    private const uint VK_OEM_3  = 0xC0;

    private bool _overlayVisible = true;

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

        RegisterHotKey(hwnd, HOTKEY_QUIT, MOD_CTRL | MOD_SHIFT, VK_Q);
        RegisterHotKey(hwnd, HOTKEY_SETTINGS, MOD_CTRL | MOD_SHIFT, VK_S);
        RegisterHotKey(hwnd, HOTKEY_TOGGLE, 0, VK_OEM_3);  // backtick toggles overlay

        var source = HwndSource.FromHwnd(hwnd);
        source?.AddHook(WndProc);

        // Load saved settings into PolarRing
        _settingsWindow = new SettingsWindow(PolarRing);

        StatusDot.Opacity = 1.0;
        StartPipeListener();
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        const int WM_HOTKEY = 0x0312;
        if (msg == WM_HOTKEY)
        {
            int id = wParam.ToInt32();
            if (id == HOTKEY_QUIT)
            {
                handled = true;
                Close();
            }
            else if (id == HOTKEY_SETTINGS)
            {
                handled = true;
                ToggleSettings();
            }
            else if (id == HOTKEY_TOGGLE)
            {
                handled = true;
                _overlayVisible = !_overlayVisible;
                PolarRing.Visibility = _overlayVisible ? Visibility.Visible : Visibility.Hidden;
            }
        }
        return IntPtr.Zero;
    }

    private void ToggleSettings()
    {
        if (_settingsWindow == null) return;

        if (_settingsWindow.IsVisible)
            _settingsWindow.Hide();
        else
            _settingsWindow.Show();
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        var hwnd = new WindowInteropHelper(this).Handle;
        UnregisterHotKey(hwnd, HOTKEY_QUIT);
        UnregisterHotKey(hwnd, HOTKEY_SETTINGS);
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
        _topmostTimer?.Stop();
        _decayTimer?.Stop();
        _cts?.Cancel();
        _pipeClient?.Dispose();
        _settingsWindow?.Close();
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
