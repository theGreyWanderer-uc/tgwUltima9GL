using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;

namespace U9GL.WebDashboard.Telemetry;

internal static class DashboardTelemetryEndpoints
{
    internal static void MapDashboardTelemetry(this WebApplication application, DashboardTelemetryStore store,
        DashboardTelemetryHub hub, CancellationToken stopping, GameLogFollower log)
    {
        application.MapGet("/api/v1/log", (HttpContext context) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            long after = long.TryParse(context.Request.Query["after"], out long a) && a >= 0 ? a : 0;
            int limit = int.TryParse(context.Request.Query["limit"], out int l) ? l : GameLogFollower.PageLimit;
            return Results.Json(log.Page(after, limit), DashboardTelemetryStore.Json);
        });
        application.MapGet("/api/v1/telemetry", (HttpContext context) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            return Results.Json(store.GetView(), DashboardTelemetryStore.Json);
        });
        application.MapGet("/api/v1/events", async (HttpContext context) =>
        {
            using var subscription = hub.Subscribe();
            if (subscription == null)
            {
                context.Response.StatusCode = StatusCodes.Status503ServiceUnavailable;
                context.Response.Headers.RetryAfter = "5";
                return;
            }
            context.Response.ContentType = "text/event-stream";
            context.Response.Headers.CacheControl = "no-store";
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted, stopping);
            try
            {
                await foreach (string json in subscription.Reader.ReadAllAsync(linked.Token))
                {
                    using var sendTimeout = CancellationTokenSource.CreateLinkedTokenSource(linked.Token);
                    sendTimeout.CancelAfter(TimeSpan.FromSeconds(5));
                    await context.Response.WriteAsync("event: telemetry\ndata: " + json + "\n\n", sendTimeout.Token);
                    await context.Response.Body.FlushAsync(sendTimeout.Token);
                }
            }
            catch (OperationCanceledException) { }
            catch (IOException) { }
        });
    }
}
