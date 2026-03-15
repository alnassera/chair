using System;
using System.IO;
using System.IO.Pipes;
using System.Text.Json;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace ChairOverlay;

/// Single aggregated direction reading per hop
public record DirectionReading(float Angle, float EnergyDb, float Confidence);

public class PipeClient : IDisposable
{
    private const string PipeName = "chair-audio-events";
    private NamedPipeClientStream? _pipe;
    private readonly Channel<DirectionReading> _channel = Channel.CreateBounded<DirectionReading>(128);

    public ChannelReader<DirectionReading> Readings => _channel.Reader;

    public Task RunAsync(CancellationToken ct)
    {
        return Task.Run(async () =>
        {
            try { await ReadLoop(ct); }
            finally { _channel.Writer.TryComplete(); }
        }, ct);
    }

    private async Task ReadLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                _pipe?.Dispose();
                _pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.In);

                Console.WriteLine("[pipe] Connecting to audio engine...");
                await _pipe.ConnectAsync(ct);
                Console.WriteLine("[pipe] Connected.");

                using var reader = new StreamReader(_pipe, leaveOpen: false);
                while (!ct.IsCancellationRequested)
                {
                    var line = await reader.ReadLineAsync(ct);
                    if (line is null) break;

                    var r = Parse(line);
                    if (r is not null)
                        await _channel.Writer.WriteAsync(r, ct);
                }

                Console.WriteLine("[pipe] Pipe closed, reconnecting...");
            }
            catch (OperationCanceledException) { return; }
            catch (Exception ex)
            {
                Console.WriteLine($"[pipe] Error: {ex.Message}, reconnecting...");
            }

            _pipe?.Dispose();
            _pipe = null;

            try { await Task.Delay(500, ct); }
            catch (OperationCanceledException) { return; }
        }
    }

    private static DirectionReading? Parse(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            // Skip old-format band arrays
            if (root.TryGetProperty("type", out _)) return null;

            float angle = root.GetProperty("a").GetSingle();
            float db = root.GetProperty("e").GetSingle();
            float conf = root.GetProperty("c").GetSingle();
            return new DirectionReading(angle, db, conf);
        }
        catch { return null; }
    }

    public void Dispose() { _pipe?.Dispose(); }
}
