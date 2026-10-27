using System.IO.Pipes;
using System.Text;

namespace Devil.Game;

internal sealed class LabCommandServer : IDisposable
{
    private readonly CancellationTokenSource _cancellation = new();
    private readonly EventWaitHandle _presence;
    private readonly string _pipeName;
    private readonly Task _listener;

    public LabCommandServer(Action<int, string[]> receive, string pipeName = "DEViL.Lab", string presenceName = @"Local\DEViL.Lab.Active")
    {
        _pipeName = pipeName;
        _presence = new EventWaitHandle(true, EventResetMode.ManualReset, presenceName);
        _listener = Listen(receive);
    }

    private async Task Listen(Action<int, string[]> receive)
    {
        while (!_cancellation.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeServerStream(_pipeName, PipeDirection.In, 1,
                    PipeTransmissionMode.Byte, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                await pipe.WaitForConnectionAsync(_cancellation.Token);
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(_cancellation.Token);
                timeout.CancelAfter(TimeSpan.FromSeconds(2));
                // The native client sends one small UTF-8 line. Never wait for
                // arbitrary-length input or execute shell/engine command text.
                byte[] data = new byte[2048]; int length = 0;
                while (length < data.Length)
                {
                    int read = await pipe.ReadAsync(data.AsMemory(length), timeout.Token);
                    if (read == 0) break;
                    length += read;
                    if (Array.IndexOf(data, (byte)'\n', 0, length) >= 0) break;
                }
                string text = Encoding.UTF8.GetString(data, 0, length).TrimEnd('\r', '\n');
                string[] fields = text.Split('\t');
                if (fields.Length is >= 2 and <= 130 && int.TryParse(fields[0], out int process))
                    try { receive(process, fields[1..]); } catch (InvalidOperationException) { }
            }
            catch (OperationCanceledException) { }
            catch (IOException) { if (!_cancellation.IsCancellationRequested) await Task.Delay(100); }
            catch (UnauthorizedAccessException) { break; }
        }
    }

    public void Dispose()
    {
        _cancellation.Cancel();
        _presence.Dispose();
        // Do not block the UI thread waiting for a pending pipe connection.
        _ = _listener.ContinueWith(_ => _cancellation.Dispose(), TaskScheduler.Default);
    }
}
