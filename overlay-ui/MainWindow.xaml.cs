using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace ChairOverlay;

public partial class MainWindow : Window
{
    private PipeClient? _pipeClient;
    private CancellationTokenSource? _cts;
    private DispatcherTimer? _topmostTimer;
    private DispatcherTimer? _decayTimer;

    // Direction ring: one indicator per direction
    private DateTime[] _dirExpiry = new DateTime[5];
    private UIElement[]? _dirIndicators;

    // Log lines: separate rolling logs per side
    private struct LogEntry
    {
        public string Text;
        public DateTime Expiry;
    }
    private LogEntry[] _leftLog = new LogEntry[3];
    private LogEntry[] _rightLog = new LogEntry[3];

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

    private static readonly TimeSpan PersistDuration = TimeSpan.FromMilliseconds(400);
    private static readonly TimeSpan FadeDuration = TimeSpan.FromMilliseconds(300);
    private static readonly TimeSpan LogPersist = TimeSpan.FromMilliseconds(2000);

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
        // Map direction indices to XAML indicators
        _dirIndicators = new UIElement[] {
            DirHardLeft, DirLeft, DirCenter, DirRight, DirHardRight
        };

        var hwnd = new WindowInteropHelper(this).Handle;

        int style = GetWindowLong(hwnd, GWL_EXSTYLE);
        SetWindowLong(hwnd, GWL_EXSTYLE, style | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
        ForceTopmost(hwnd);

        _topmostTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _topmostTimer.Tick += (_, _) => ForceTopmost(hwnd);
        _topmostTimer.Start();

        _decayTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
        _decayTimer.Tick += (_, _) => CheckDecay();
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
            var reader = _pipeClient.Events;
            while (await reader.WaitToReadAsync(_cts.Token))
            {
                while (reader.TryRead(out var evt))
                {
                    Dispatcher.Invoke(() =>
                    {
                        StatusDot.Fill = new System.Windows.Media.SolidColorBrush(
                            System.Windows.Media.Color.FromArgb(180, 0, 200, 0));
                        ShowEvent(evt);
                    });
                }
            }
        }, _cts.Token);
    }

    private static string? ClassDisplayName(string cls) => cls switch
    {
        "footsteps" => "STEPS",
        "gunfire"   => "FIRE",
        "reload"    => "RELOAD",
        "global"    => "CUE",
        _ => null,
    };

    private static string DirectionArrow(string dir) => dir switch
    {
        "hard_left"  => "<<",
        "left"       => "<",
        "hard_right" => ">>",
        "right"      => ">",
        _ => "",
    };

    private static int DirectionIndex(string dir) => dir switch
    {
        "hard_left"  => 0,
        "left"       => 1,
        "center"     => 2,
        "right"      => 3,
        "hard_right" => 4,
        _ => -1,
    };

    private void ShowEvent(AudioEventData evt)
    {
        var label = ClassDisplayName(evt.SoundClass);
        if (label is null) return;

        var now = DateTime.UtcNow;
        bool isLeft = evt.Direction is "hard_left" or "left";
        bool isRight = evt.Direction is "hard_right" or "right";

        // --- Crosshair ring indicator ---
        int dirIdx = DirectionIndex(evt.Direction);
        if (dirIdx >= 0 && _dirIndicators != null)
        {
            _dirExpiry[dirIdx] = now + PersistDuration;
            _dirIndicators[dirIdx].BeginAnimation(OpacityProperty, null);
            _dirIndicators[dirIdx].Opacity = 1.0;
        }

        // --- Positional text log (left/right only, center = ring only) ---
        if (!isLeft && !isRight) return;

        string arrow = DirectionArrow(evt.Direction);
        string logText = isLeft ? $"{arrow} {label}" : $"{label} {arrow}";

        if (isLeft)
            PushToLog(_leftLog, LeftLogLines(), logText, now + LogPersist);
        else
            PushToLog(_rightLog, RightLogLines(), logText, now + LogPersist);
    }

    private TextBlock[] LeftLogLines() => new[] { LeftLogLine1, LeftLogLine2, LeftLogLine3 };
    private TextBlock[] RightLogLines() => new[] { RightLogLine1, RightLogLine2, RightLogLine3 };

    private void PushToLog(LogEntry[] log, TextBlock[] lines, string text, DateTime expiry)
    {
        var now = DateTime.UtcNow;

        if (log[0].Text == text && log[0].Expiry > now)
        {
            log[0].Expiry = expiry;
            return;
        }

        log[2] = log[1];
        log[1] = log[0];
        log[0] = new LogEntry { Text = text, Expiry = expiry };

        RefreshLogPanel(log, lines);
    }

    private void RefreshLogPanel(LogEntry[] log, TextBlock[] lines)
    {
        var now = DateTime.UtcNow;
        for (int i = 0; i < lines.Length; i++)
        {
            if (log[i].Expiry > now)
            {
                lines[i].Text = log[i].Text;
                lines[i].BeginAnimation(OpacityProperty, null);
                lines[i].Opacity = 1.0;
            }
        }
    }

    private void CheckDecay()
    {
        var now = DateTime.UtcNow;

        // Direction ring indicators
        if (_dirIndicators != null)
        {
            for (int i = 0; i < 5; i++)
            {
                if (_dirExpiry[i] != DateTime.MinValue && now > _dirExpiry[i])
                {
                    FadeOut(_dirIndicators[i]);
                    _dirExpiry[i] = DateTime.MinValue;
                }
            }
        }

        // Log lines
        DecayLog(_leftLog, LeftLogLines());
        DecayLog(_rightLog, RightLogLines());
    }

    private void DecayLog(LogEntry[] log, TextBlock[] lines)
    {
        var now = DateTime.UtcNow;
        for (int i = 0; i < log.Length; i++)
        {
            if (log[i].Expiry != DateTime.MinValue && now > log[i].Expiry)
            {
                FadeOut(lines[i]);
                log[i].Expiry = DateTime.MinValue;
            }
        }
    }

    private void FadeOut(UIElement target)
    {
        var anim = new DoubleAnimation(0.0, FadeDuration)
        {
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseIn }
        };
        target.BeginAnimation(OpacityProperty, anim);
    }
}
