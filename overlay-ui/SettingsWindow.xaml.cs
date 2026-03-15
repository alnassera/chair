using System;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;

namespace ChairOverlay;

public partial class SettingsWindow : Window
{
    private readonly PolarRingControl _ring;
    private bool _loading = true;

    private static readonly string SettingsPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "CHAIR", "settings.json");

    public SettingsWindow(PolarRingControl ring)
    {
        _ring = ring;
        InitializeComponent();
        LoadSettings();
        _loading = false;
        UpdateAllLabels();
    }

    private void OnSliderChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_loading || sender is not Slider sl) return;

        double v = sl.Value;
        switch (sl.Tag as string)
        {
            case "RadiusBase":      _ring.RadiusBase = v; break;
            case "SpikeMax":        _ring.SpikeMax = v; break;
            case "DbFloor":         _ring.DbFloor = v; break;
            case "DbCeil":          _ring.DbCeil = v; break;
            case "SpikeWidth":      _ring.SpikeWidth = v; break;
            case "DecayRate":       _ring.DecayRate = v; break;
            case "AngleSmooth":     _ring.AngleSmooth = v; break;
            case "CenterDeadzone":  _ring.CenterDeadzone = v; break;
            case "CenterSmooth":    _ring.CenterSmooth = v; break;
            case "StrokeThickness": _ring.StrokeThickness = v; break;
            case "ArcOpacity":      _ring.ArcOpacity = v; break;
        }

        UpdateAllLabels();
        SaveSettings();
    }

    private void UpdateAllLabels()
    {
        if (ValRadius != null) ValRadius.Text = $"{SlRadius.Value:F0}";
        if (ValArcOpacity != null) ValArcOpacity.Text = $"{SlArcOpacity.Value:F2}";
        if (ValStroke != null) ValStroke.Text = $"{SlStroke.Value:F1}";
        if (ValSpikeMax != null) ValSpikeMax.Text = $"{SlSpikeMax.Value:F0}";
        if (ValSpikeWidth != null) ValSpikeWidth.Text = $"{SlSpikeWidth.Value:F1}";
        if (ValDecay != null) ValDecay.Text = $"{SlDecay.Value:F2}";
        if (ValDbFloor != null) ValDbFloor.Text = $"{SlDbFloor.Value:F0}";
        if (ValDbCeil != null) ValDbCeil.Text = $"{SlDbCeil.Value:F0}";
        if (ValAngleSmooth != null) ValAngleSmooth.Text = $"{SlAngleSmooth.Value:F2}";
        if (ValDeadzone != null) ValDeadzone.Text = $"{SlDeadzone.Value:F0}";
        if (ValCenterSmooth != null) ValCenterSmooth.Text = $"{SlCenterSmooth.Value:F2}";
    }

    private void OnReset(object sender, RoutedEventArgs e)
    {
        _loading = true;
        SlRadius.Value = 120;
        SlArcOpacity.Value = 0.13;
        SlStroke.Value = 2.5;
        SlSpikeMax.Value = 70;
        SlSpikeWidth.Value = 5;
        SlDecay.Value = 0.85;
        SlDbFloor.Value = -30;
        SlDbCeil.Value = 0;
        SlAngleSmooth.Value = 0.5;
        SlDeadzone.Value = 15;
        SlCenterSmooth.Value = 0.15;
        _loading = false;

        // Apply all
        _ring.RadiusBase = 120;
        _ring.SpikeMax = 70;
        _ring.DbFloor = -30;
        _ring.DbCeil = 0;
        _ring.SpikeWidth = 5;
        _ring.DecayRate = 0.85;
        _ring.AngleSmooth = 0.5;
        _ring.CenterDeadzone = 15;
        _ring.CenterSmooth = 0.15;
        _ring.StrokeThickness = 2.5;
        _ring.ArcOpacity = 0.13;

        UpdateAllLabels();
        SaveSettings();
    }

    private void SaveSettings()
    {
        try
        {
            var dir = Path.GetDirectoryName(SettingsPath)!;
            Directory.CreateDirectory(dir);

            var data = new
            {
                RadiusBase = _ring.RadiusBase,
                SpikeMax = _ring.SpikeMax,
                DbFloor = _ring.DbFloor,
                DbCeil = _ring.DbCeil,
                SpikeWidth = _ring.SpikeWidth,
                DecayRate = _ring.DecayRate,
                AngleSmooth = _ring.AngleSmooth,
                CenterDeadzone = _ring.CenterDeadzone,
                CenterSmooth = _ring.CenterSmooth,
                StrokeThickness = _ring.StrokeThickness,
                ArcOpacity = _ring.ArcOpacity,
            };

            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { /* ignore save errors */ }
    }

    private void LoadSettings()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return;

            using var doc = JsonDocument.Parse(File.ReadAllText(SettingsPath));
            var r = doc.RootElement;

            if (r.TryGetProperty("RadiusBase", out var v)) SlRadius.Value = _ring.RadiusBase = v.GetDouble();
            if (r.TryGetProperty("SpikeMax", out v)) SlSpikeMax.Value = _ring.SpikeMax = v.GetDouble();
            if (r.TryGetProperty("DbFloor", out v)) SlDbFloor.Value = _ring.DbFloor = v.GetDouble();
            if (r.TryGetProperty("DbCeil", out v)) SlDbCeil.Value = _ring.DbCeil = v.GetDouble();
            if (r.TryGetProperty("SpikeWidth", out v)) SlSpikeWidth.Value = _ring.SpikeWidth = v.GetDouble();
            if (r.TryGetProperty("DecayRate", out v)) SlDecay.Value = _ring.DecayRate = v.GetDouble();
            if (r.TryGetProperty("AngleSmooth", out v)) SlAngleSmooth.Value = _ring.AngleSmooth = v.GetDouble();
            if (r.TryGetProperty("CenterDeadzone", out v)) SlDeadzone.Value = _ring.CenterDeadzone = v.GetDouble();
            if (r.TryGetProperty("CenterSmooth", out v)) SlCenterSmooth.Value = _ring.CenterSmooth = v.GetDouble();
            if (r.TryGetProperty("StrokeThickness", out v)) SlStroke.Value = _ring.StrokeThickness = v.GetDouble();
            if (r.TryGetProperty("ArcOpacity", out v)) SlArcOpacity.Value = _ring.ArcOpacity = v.GetDouble();
        }
        catch { /* ignore load errors */ }
    }

    protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        // Hide instead of close — can reopen with hotkey
        e.Cancel = true;
        Hide();
    }
}
