namespace Prani.Engine;

/// <summary>
/// Last-resort crash reporter: appends unhandled exceptions to prani-crash.log next to
/// the executable, so "the app just closed" always leaves a stack trace to read.
/// </summary>
public static class CrashLog
{
    public static string FilePath => Path.Combine(AppContext.BaseDirectory, "prani-crash.log");

    public static void Write(string source, Exception? ex)
    {
        try
        {
            File.AppendAllText(FilePath,
                $"==== {DateTime.Now:yyyy-MM-dd HH:mm:ss} [{source}] ===={Environment.NewLine}" +
                $"{ex}{Environment.NewLine}{Environment.NewLine}");
        }
        catch
        {
            // Never throw from the crash reporter itself.
        }
    }
}
