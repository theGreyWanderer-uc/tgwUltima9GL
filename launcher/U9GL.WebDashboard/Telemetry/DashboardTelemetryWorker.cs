namespace U9GL.WebDashboard.Telemetry;

internal static class DashboardTelemetryWorker
{
    internal static async Task RunAsync(Func<IDashboardTelemetrySource>? factory, DashboardTelemetryStore store,
        DashboardTelemetryHub hub, CancellationToken cancellationToken, GameLogFollower? log = null)
    {
        try
        {
            using var source = factory?.Invoke();
            using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(100));
            int polls = 0;
            do
            {
                try
                {
                    if (source != null) store.Apply(source.Poll());
                    if (log != null && polls % 5 == 0)
                    {
                        try { log.Poll(); }
                        catch (Exception e) when (e is not OperationCanceledException) {  }
                    }
                    if (polls++ % 5 == 0) hub.Publish();
                }
                catch (Exception e) { store.Apply(new("error", $"Telemetry worker: {e.GetType().Name}: {e.Message}")); }
            } while (await timer.WaitForNextTickAsync(cancellationToken).ConfigureAwait(false));
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { }
        catch (Exception e)
        {
            store.Apply(new("error", "Telemetry worker initialization or disposal: " + e.Message));
            hub.Publish();
        }
    }
}
