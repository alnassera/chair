using System;
using System.IO;
using System.IO.Pipes;
using System.Text.Json;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace ChairOverlay;

public record AudioEventData(
    string SoundClass,
    string Direction,
    float Confidence,
    float Db,
    float TimestampMs,
    uint Band,
    float FreqLo,
    float FreqHi
);

public class PipeClient : IDisposable
{
    private const string PipeName = "chair-audio-events";
    private NamedPipeClientStream? _pipe;
    private readonly Channel<AudioEventData> _channel = Channel.CreateBounded<AudioEventData>(64);

    /// <summary>
    /// Returns a ChannelReader that yields events as soon as they arrive.
    /// </summary>
    public ChannelReader<AudioEventData> Events => _channel.Reader;

    /// <summary>
    /// Start the background read loop. Call once.
    /// </summary>
    public Task RunAsync(CancellationToken ct)
    {
        return Task.Run(async () =>
        {
            try
            {
                await ReadLoop(ct);
            }
            finally
            {
                _channel.Writer.TryComplete();
            }
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

                    var evt = ParseEvent(line);
                    if (evt is not null)
                        await _channel.Writer.WriteAsync(evt, ct);
                }

                Console.WriteLine("[pipe] Pipe closed, reconnecting...");
            }
            catch (OperationCanceledException)
            {
                return;
            }
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

    private static AudioEventData? ParseEvent(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            return new AudioEventData(
                SoundClass:  root.GetProperty("class").GetString() ?? "unknown",
                Direction:   root.GetProperty("dir").GetString() ?? "center",
                Confidence:  root.GetProperty("conf").GetSingle(),
                Db:          root.GetProperty("db").GetSingle(),
                TimestampMs: root.GetProperty("t").GetSingle(),
                Band:        root.GetProperty("band").GetUInt32(),
                FreqLo:      root.GetProperty("freqLo").GetSingle(),
                FreqHi:      root.GetProperty("freqHi").GetSingle()
            );
        }
        catch
        {
            Console.WriteLine($"[pipe] Failed to parse: {json}");
            return null;
        }
    }

    public void Dispose()
    {
        _pipe?.Dispose();
    }
}
