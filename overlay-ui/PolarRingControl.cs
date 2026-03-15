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

    private readonly double[] _energy = new double[180];
    private double _angle = 90.0;
    private double _amp = 0.0;

    public void Push(float angleDeg, float energyDb, float confidence)
    {
        double t = Math.Clamp((energyDb - DbFloor) / (DbCeil - DbFloor), 0.0, 1.0);
        double amplitude = t * SpikeMax;
        if (amplitude < 1.0) return;

        double angle = Math.Clamp((double)angleDeg, 0.0, 180.0);

        double distFromCenter = Math.Abs(_angle - 90.0);
        double smooth = distFromCenter < CenterDeadzone ? CenterSmooth : AngleSmooth;

        _angle += (angle - _angle) * smooth;
        _angle = Math.Clamp(_angle, 0.0, 180.0);

        // Smooth rise instead of instant snap — blends consecutive events
        if (amplitude > _amp)
            _amp += (amplitude - _amp) * AttackSmooth;

    }

    public void Tick()
    {
        _amp *= DecayRate;
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

        if (_amp < 0.5) return;

        // Render a single spike directly from tracked angle + amplitude
        // No energy array accumulation — prevents ghost spikes from angle jitter
        var geo = new StreamGeometry();
        using (var ctx = geo.Open())
        {
            int center = (int)Math.Round(Math.Clamp(_angle, 0.0, 179.0));
            int spread = (int)Math.Ceiling(SpikeWidth * 3.0);
            int lo = Math.Max(0, center - spread);
            int hi = Math.Min(180, center + spread);

            for (int deg = 0; deg <= 180; deg++)
            {
                double spike = 0.0;
                if (deg >= lo && deg <= hi)
                {
                    double offset = deg - _angle;
                    spike = _amp * Math.Exp(-0.5 * (offset / SpikeWidth) * (offset / SpikeWidth));
                }

                double r = RadiusBase + spike;
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
