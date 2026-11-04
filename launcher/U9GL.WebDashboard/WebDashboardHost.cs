using System.Net;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using U9GL.WebDashboard.Telemetry;

namespace U9GL.WebDashboard;

public sealed class WebDashboardHost(int port = WebDashboardHost.DefaultPort,
    Func<IDashboardTelemetrySource>? sourceFactory = null, GameLogFollower? log = null) : IAsyncDisposable
{
    public const int DefaultPort = 9999;
    public static readonly TimeSpan ShutdownTimeout = TimeSpan.FromSeconds(3);
    private WebApplication? _application;
    private CancellationTokenSource? _workerStop;
    private Task? _worker;
    public DashboardTelemetryStore Telemetry { get; } = new();
    public GameLogFollower Log { get; } = log ?? new GameLogFollower();

    public Uri? Address { get; private set; }

    public string? NetworkToken { get; set; }
    private string? _networkToken;

    public IReadOnlyList<Uri> NetworkLinks { get; private set; } = [];
    public IReadOnlyList<string> NetworkInterfaces { get; private set; } = [];

    internal const string TokenCookie = "u9gl_token";

    public async Task StartAsync(CancellationToken cancellationToken = default)
    {
        if (_application != null) return;
        ArgumentOutOfRangeException.ThrowIfNegative(port);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(port, 65535);
        cancellationToken.ThrowIfCancellationRequested();

        var builder = WebApplication.CreateSlimBuilder(new WebApplicationOptions
        {
            Args = [],
            ApplicationName = typeof(WebDashboardHost).Assembly.GetName().Name,
            ContentRootPath = AppContext.BaseDirectory,
        });
        builder.Configuration.Sources.Clear();
        builder.Logging.ClearProviders();
        _networkToken = NetworkToken;
        builder.WebHost.ConfigureKestrel(options => options.Listen(_networkToken != null ? IPAddress.Any : IPAddress.Loopback, port));
        builder.Services.Configure<HostOptions>(options => options.ShutdownTimeout = ShutdownTimeout);
        builder.Services.AddSingleton<IHostLifetime, LauncherHostLifetime>();

        var application = builder.Build();
        var workerStop = new CancellationTokenSource();
        try
        {
            application.Use((context, next) =>
            {
                switch (Admit(context.Connection.RemoteIpAddress, context.Request.Host.Host, context.Request.Path,
                    context.Request.Query["t"], context.Request.Cookies[TokenCookie], _networkToken))
                {
                    case Admission.Serve: return next(context);
                    case Admission.ServeAndRemember:
                        context.Response.Cookies.Append(TokenCookie, _networkToken!, new CookieOptions
                        {
                            HttpOnly = true, SameSite = SameSiteMode.Lax, Path = "/", IsEssential = true,
                        });
                        return next(context);
                    case Admission.NeedsToken:
                        context.Response.StatusCode = StatusCodes.Status403Forbidden;
                        context.Response.ContentType = "text/plain; charset=utf-8";
                        return context.Response.WriteAsync("This U9GL dashboard needs the link shown in the U9GL launcher.");
                    default:
                        context.Response.StatusCode = StatusCodes.Status400BadRequest;
                        return Task.CompletedTask;
                }
            });
            application.MapDashboardAssets();
            var hub = new DashboardTelemetryHub(Telemetry);
            Telemetry.LogSummary = Log.Summary;
            application.MapDashboardTelemetry(Telemetry, hub, workerStop.Token, Log);

            await application.StartAsync(cancellationToken).ConfigureAwait(false);
            var server = application.Services.GetRequiredService<IServer>();
            string address = server.Features.Get<IServerAddressesFeature>()!.Addresses.Single();
            var bound = new Uri(address.Replace("0.0.0.0", "127.0.0.1", StringComparison.Ordinal));
            Address = bound;
            var local = _networkToken == null ? [] : LocalAddresses().ToArray();
            NetworkLinks = local.Select(l => new Uri($"http://{l.Address}:{bound.Port}/?t={Uri.EscapeDataString(_networkToken!)}")).ToArray();
            NetworkInterfaces = local.Select(l => l.Interface).ToArray();
            _application = application;
            _workerStop = workerStop;
            _worker = Task.Run(() => DashboardTelemetryWorker.RunAsync(sourceFactory, Telemetry, hub, workerStop.Token, Log));
        }
        catch
        {
            workerStop.Dispose();
            await application.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    public async ValueTask DisposeAsync()
    {
        var application = _application;
        _application = null;
        Address = null;
        NetworkLinks = [];
        NetworkInterfaces = [];
        if (application == null) return;
        _workerStop!.Cancel();
        using var timeout = new CancellationTokenSource(ShutdownTimeout);
        try
        {
            try { if (_worker != null) await _worker.ConfigureAwait(false); }
            catch (OperationCanceledException) { }
            await application.StopAsync(timeout.Token).ConfigureAwait(false);
        }
        finally
        {
            await application.DisposeAsync().ConfigureAwait(false);
            _workerStop.Dispose();
            _workerStop = null;
            _worker = null;
        }
    }

    internal enum Admission { Serve, ServeAndRemember, NeedsToken, Refuse }

    internal static Admission Admit(IPAddress? remote, string host, string path, string? queryToken, string? cookieToken, string? token)
    {
        if (remote == null || IPAddress.IsLoopback(remote)) return IsLoopbackName(host) ? Admission.Serve : Admission.Refuse;
        if (token == null || !IsNetworkName(host)) return Admission.Refuse;
        if (TokenMatches(queryToken, token)) return Admission.ServeAndRemember;
        if (TokenMatches(cookieToken, token)) return Admission.Serve;
        return path.StartsWith("/api/", StringComparison.Ordinal) ? Admission.NeedsToken : Admission.Serve;
    }

    public static string NewNetworkToken() =>
        Convert.ToBase64String(System.Security.Cryptography.RandomNumberGenerator.GetBytes(16)).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private static bool TokenMatches(string? offered, string token) =>
        offered != null && System.Security.Cryptography.CryptographicOperations.FixedTimeEquals(
            System.Text.Encoding.UTF8.GetBytes(offered), System.Text.Encoding.UTF8.GetBytes(token));

    internal static bool IsNetworkName(string host) =>
        (IPAddress.TryParse(host, out var ip) && ip.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork) ||
        string.Equals(host, Environment.MachineName, StringComparison.OrdinalIgnoreCase);

    internal static IEnumerable<(IPAddress Address, string Interface)> LocalAddresses()
    {
        var found = new List<(IPAddress Address, string Interface, bool Gateway)>();
        foreach (var n in System.Net.NetworkInformation.NetworkInterface.GetAllNetworkInterfaces())
        {
            if (n.OperationalStatus != System.Net.NetworkInformation.OperationalStatus.Up ||
                n.NetworkInterfaceType is System.Net.NetworkInformation.NetworkInterfaceType.Loopback or System.Net.NetworkInformation.NetworkInterfaceType.Tunnel)
                continue;
            var properties = n.GetIPProperties();
            bool gateway = properties.GatewayAddresses.Any(g => g.Address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork &&
                                                                !g.Address.Equals(IPAddress.Any));
            foreach (var a in properties.UnicastAddresses.Select(u => u.Address))
            {
                byte[] b = a.GetAddressBytes();
                if (a.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork || IPAddress.IsLoopback(a) || (b[0] == 169 && b[1] == 254)) continue;
                found.Add((a, n.Name, gateway));
            }
        }
        return found.DistinctBy(f => f.Address).OrderByDescending(f => f.Gateway).Select(f => (f.Address, f.Interface));
    }

    internal static bool IsLoopbackName(string host) =>
        string.Equals(host, "127.0.0.1", StringComparison.Ordinal) ||
        string.Equals(host, "localhost", StringComparison.OrdinalIgnoreCase);

    private sealed class LauncherHostLifetime : IHostLifetime
    {
        public Task WaitForStartAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
    }
}
