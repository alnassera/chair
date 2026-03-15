using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;

namespace ChairOverlay;

public class PolarRingControl : FrameworkElement
{
    private const double R_BASE = 210.0;
    private const double MAX_SPIKE = 90.0;
    private const double DB_FLOOR = -30.0;
    private const double DB_CEIL = 0.0;
    private const double SPLAT_SIGMA = 5.0;
    private const double FALL_RATE = 0.85;
    private const double ANGLE_SMOOTH = 0.06;
    private const double AMP_SMOOTH = 0.3;
    private const double SPLIT_ANGLE = 30.0;    // degrees apart to spawn a second spike

    private static readonly Pen RingPen = new(new SolidColorBrush(Color.FromArgb(0x20, 0xFF, 0xFF, 0xFF)), 1.0);
    private static readonly Pen BulgePen = new(new SolidColorBrush(Color.FromArgb(0xDD, 0xFF, 0xFF, 0xFF)), 2.5);

    static PolarRingControl()
    {
        RingPen.Brush.Freeze(); RingPen.Freeze();
        BulgePen.Brush.Freeze(); BulgePen.Freeze();
    }

    private readonly double[] _energy = new double[360];

    private class Tracker
    {
        public double Angle;
        public double Amplitude;
    }

    private readonly List<Tracker> _trackers = new();

    public void Push(float angleDeg, float energyDb, float confidence)
    {
        double t = Math.Clamp((energyDb - DB_FLOOR) / (DB_CEIL - DB_FLOOR), 0.0, 1.0);
        double amplitude = t * MAX_SPIKE;
        if (amplitude < 1.0) return;

        // Find the closest tracker
        Tracker? closest = null;
        double closestDist = double.MaxValue;
        foreach (var tr in _trackers)
        {
            double diff = angleDeg - tr.Angle;
            while (diff > 180.0) diff -= 360.0;
            while (diff < -180.0) diff += 360.0;
            double dist = Math.Abs(diff);
            if (dist < closestDist)
            {
                closestDist = dist;
                closest = tr;
            }
        }

        if (closest != null && closestDist < SPLIT_ANGLE)
        {
            // Update existing tracker
            double diff = angleDeg - closest.Angle;
            while (diff > 180.0) diff -= 360.0;
            while (diff < -180.0) diff += 360.0;
            closest.Angle += diff * ANGLE_SMOOTH;
            while (closest.Angle < 0) closest.Angle += 360.0;
            while (closest.Angle >= 360) closest.Angle -= 360.0;

            if (amplitude > closest.Amplitude)
                closest.Amplitude += (amplitude - closest.Amplitude) * AMP_SMOOTH;
        }
        else
        {
            // New source — spawn tracker (max 4)
            if (_trackers.Count >= 4)
            {
                // Replace weakest
                Tracker? weakest = _trackers[0];
                foreach (var tr in _trackers)
                    if (tr.Amplitude < weakest.Amplitude) weakest = tr;
                weakest.Angle = angleDeg;
                weakest.Amplitude = amplitude;
            }
            else
            {
                _trackers.Add(new Tracker { Angle = angleDeg, Amplitude = amplitude });
            }
        }

        // Rebuild energy ring from all trackers
        for (int i = 0; i < 360; i++)
            _energy[i] *= 0.7;

        foreach (var tr in _trackers)
        {
            if (tr.Amplitude < 0.5) continue;
            int center = ((int)Math.Round(tr.Angle)) % 360;
            if (center < 0) center += 360;
            int spread = (int)Math.Ceiling(SPLAT_SIGMA * 3.0);

            for (int offset = -spread; offset <= spread; offset++)
            {
                int idx = (center + offset + 360) % 360;
                double g = tr.Amplitude * Math.Exp(-0.5 * (offset / SPLAT_SIGMA) * (offset / SPLAT_SIGMA));
                if (g > _energy[idx])
                    _energy[idx] = g;
            }
        }

        InvalidateVisual();
    }

    public void Tick()
    {
        bool any = false;
        for (int i = 0; i < 360; i++)
        {
            _energy[i] *= FALL_RATE;
            if (_energy[i] < 0.3) _energy[i] = 0.0;
            else any = true;
        }

        // Decay trackers, remove dead ones
        for (int i = _trackers.Count - 1; i >= 0; i--)
        {
            _trackers[i].Amplitude *= FALL_RATE;
            if (_trackers[i].Amplitude < 0.3)
                _trackers.RemoveAt(i);
        }

        if (any) InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        double cx = ActualWidth / 2.0;
        double cy = ActualHeight / 2.0;

        dc.DrawEllipse(null, RingPen, new Point(cx, cy), R_BASE, R_BASE);

        bool any = false;
        for (int i = 0; i < 360; i++)
            if (_energy[i] > 0.3) { any = true; break; }
        if (!any) return;

        var geo = new StreamGeometry();
        using (var ctx = geo.Open())
        {
            for (int deg = 0; deg <= 360; deg++)
            {
                int idx = deg % 360;
                double r = R_BASE + _energy[idx];
                double rad = idx * Math.PI / 180.0;
                var pt = new Point(cx + r * Math.Cos(rad), cy - r * Math.Sin(rad));

                if (deg == 0) ctx.BeginFigure(pt, false, true);
                else ctx.LineTo(pt, true, true);
            }
        }
        geo.Freeze();
        dc.DrawGeometry(null, BulgePen, geo);
    }
}
