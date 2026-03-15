using System;
using System.Windows;
using System.Windows.Media;

namespace ChairOverlay;

public class PolarRingControl : FrameworkElement
{
    // Tunable properties — updated by settings UI
    public double RadiusBase { get; set; } = 120.0;
    public double SpikeMax { get; set; } = 100.0;
    public double DbFloor { get; set; } = -30.0;
    public double DbCeil { get; set; } = 0.0;
    public double SpikeWidth { get; set; } = 5.0;
    public double DecayRate { get; set; } = 0.85;
    public double AttackSmooth { get; set; } = 0.4;
    public double AngleSmooth { get; set; } = 0.5;
    public double CenterDeadzone { get; set; } = 15.0;
    public double CenterSmooth { get; set; } = 0.15;
    public double StrokeThickness { get; set; } = 2.5;
    public double ArcOpacity { get; set; } = 0.13;

    public int MaxTrackers { get; set; } = 4;
    public double SplitAngle { get; set; } = 30.0;

    private class Tracker
    {
        public double Angle = 90.0;
        public double Amp = 0.0;
    }

    private readonly Tracker[] _trackers = new Tracker[4]
    {
        new(), new(), new(), new()
    };

    public void Push(float angleDeg, float energyDb, float confidence)
    {
        double t = Math.Clamp((energyDb - DbFloor) / (DbCeil - DbFloor), 0.0, 1.0);
        double amplitude = t * SpikeMax;
        if (amplitude < 1.0) return;

        double angle = Math.Clamp((double)angleDeg, 0.0, 180.0);

        // Find closest active tracker, or an empty slot
        int bestIdx = -1;
        double bestDist = double.MaxValue;
        int emptyIdx = -1;

        int limit = Math.Min(MaxTrackers, _trackers.Length);
        for (int i = 0; i < limit; i++)
        {
            if (_trackers[i].Amp < 0.5)
            {
                if (emptyIdx < 0) emptyIdx = i;
                continue;
            }
            double dist = Math.Abs(_trackers[i].Angle - angle);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }

        // If closest tracker is within split threshold, merge into it
        if (bestIdx >= 0 && bestDist < SplitAngle)
        {
            var tr = _trackers[bestIdx];
            double distFromCenter = Math.Abs(tr.Angle - 90.0);
            double smooth = distFromCenter < CenterDeadzone ? CenterSmooth : AngleSmooth;
            tr.Angle += (angle - tr.Angle) * smooth;
            tr.Angle = Math.Clamp(tr.Angle, 0.0, 180.0);
            if (amplitude > tr.Amp)
                tr.Amp += (amplitude - tr.Amp) * AttackSmooth;
        }
        // Otherwise use an empty slot
        else if (emptyIdx >= 0)
        {
            var tr = _trackers[emptyIdx];
            tr.Angle = angle;
            tr.Amp = amplitude * AttackSmooth;  // soft start
        }
        // All slots full — steal the weakest
        else
        {
            int weakest = 0;
            for (int i = 1; i < limit; i++)
                if (_trackers[i].Amp < _trackers[weakest].Amp)
                    weakest = i;
            var tr = _trackers[weakest];
            tr.Angle = angle;
            tr.Amp = amplitude * AttackSmooth;
        }
    }

    public void Tick()
    {
        for (int i = 0; i < _trackers.Length; i++)
            _trackers[i].Amp *= DecayRate;
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        double cx = ActualWidth / 2.0;
        double cy = ActualHeight / 2.0;

        // Pens rebuilt from current property values
        var arcPen = new Pen(new SolidColorBrush(Color.FromArgb((byte)(ArcOpacity * 255), 0xFF, 0xFF, 0xFF)), 1.0);
        var bulgePen = new Pen(new SolidColorBrush(Color.FromArgb(0xDD, 0xFF, 0xFF, 0xFF)), StrokeThickness);

        // Semicircle arc
        var arcGeo = new StreamGeometry();
        using (var ctx = arcGeo.Open())
        {
            for (int deg = 0; deg <= 180; deg++)
            {
                double rad = deg * Math.PI / 180.0;
                var pt = new Point(cx + RadiusBase * Math.Cos(rad), cy - RadiusBase * Math.Sin(rad));
                if (deg == 0) ctx.BeginFigure(pt, false, false);
                else ctx.LineTo(pt, true, true);
            }
        }
        arcGeo.Freeze();
        dc.DrawGeometry(null, arcPen, arcGeo);

        // Render all active trackers as spikes on a single continuous path
        // First accumulate all spike contributions, then draw once
        double[] spikeEnergy = new double[181];
        bool anyActive = false;
        int limit = Math.Min(MaxTrackers, _trackers.Length);
        for (int i = 0; i < limit; i++)
        {
            var tr = _trackers[i];
            if (tr.Amp < 0.5) continue;
            anyActive = true;

            int center = (int)Math.Round(Math.Clamp(tr.Angle, 0.0, 179.0));
            int spread = (int)Math.Ceiling(SpikeWidth * 3.0);
            int lo = Math.Max(0, center - spread);
            int hi = Math.Min(180, center + spread);

            for (int deg = lo; deg <= hi; deg++)
            {
                double offset = deg - tr.Angle;
                double val = tr.Amp * Math.Exp(-0.5 * (offset / SpikeWidth) * (offset / SpikeWidth));
                // Max-merge so overlapping spikes don't stack unrealistically
                if (val > spikeEnergy[deg])
                    spikeEnergy[deg] = val;
            }
        }

        if (!anyActive) return;

        var geo = new StreamGeometry();
        using (var ctx = geo.Open())
        {
            for (int deg = 0; deg <= 180; deg++)
            {
                double r = RadiusBase + spikeEnergy[deg];
                double rad = deg * Math.PI / 180.0;
                var pt = new Point(cx + r * Math.Cos(rad), cy - r * Math.Sin(rad));
                if (deg == 0) ctx.BeginFigure(pt, false, false);
                else ctx.LineTo(pt, true, true);
            }
        }
        geo.Freeze();
        dc.DrawGeometry(null, bulgePen, geo);
    }
}
