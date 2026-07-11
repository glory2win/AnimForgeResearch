using System.Collections.Concurrent;

namespace Prani.Engine;

public enum LogLevel { Info, Warning, Error }

public readonly record struct LogEntry(DateTime Time, LogLevel Level, string Message);

/// <summary>
/// Process-wide log sink readable from both the ImGui console panel (render thread)
/// and the Avalonia log view (UI thread). Lock-free writes; readers take snapshots.
/// </summary>
public static class Log
{
    private const int Capacity = 2000;
    private static readonly ConcurrentQueue<LogEntry> Entries = new();

    /// <summary>Raised on the CALLING thread — UI subscribers must marshal themselves.</summary>
    public static event Action<LogEntry>? MessageLogged;

    public static void Info(string msg) => Write(LogLevel.Info, msg);
    public static void Warn(string msg) => Write(LogLevel.Warning, msg);
    public static void Error(string msg) => Write(LogLevel.Error, msg);

    private static void Write(LogLevel level, string msg)
    {
        var entry = new LogEntry(DateTime.Now, level, msg);
        Entries.Enqueue(entry);
        while (Entries.Count > Capacity && Entries.TryDequeue(out _)) { }
        MessageLogged?.Invoke(entry);
    }

    public static LogEntry[] Snapshot() => Entries.ToArray();
    public static void Clear() { while (Entries.TryDequeue(out _)) { } }
}
