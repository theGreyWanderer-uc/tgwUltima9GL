using System.ComponentModel;
using System.Diagnostics;
using System.Windows.Input;
using U9GL.Core.Install;
using U9GL.Launcher.Services;
using U9GL.WebDashboard;
using U9GL.WebDashboard.Telemetry;

namespace U9GL.Launcher.ViewModels;

public sealed class WebDashboardViewModel : ObservableObject
{
    private readonly LauncherSettings _settings;
    private readonly ActionLog _log;
    private readonly WebDashboardHost _host;
    private readonly SemaphoreSlim _hostGate = new(1);
    private readonly CancellationTokenSource _lifetime = new();
    private bool _closing;
    private bool _busy;
    private string _status = "Off.";

    public WebDashboardViewModel(LauncherSettings settings, ActionLog log, GameFolder folder)
    {
        _settings = settings;
        _log = log;
        _host = new(sourceFactory: () => new GameTelemetrySource(folder));
        _host.Telemetry.StateChanged += LogTelemetryState;
        OpenCommand = new RelayCommand(OpenBrowser, () => IsRunning && !_closing);
        RetryCommand = new RelayCommand(() => _ = ApplyAsync(), () => Enabled && !IsRunning && CanChange);
        CopyLinkCommand = new RelayCommand(CopyLink, () => NetworkLink.Length > 0);
        NewLinkCommand = new RelayCommand(NewLink, () => Network && CanChange);
    }

    private DateTime _telemetryLogMinute;
    private int _telemetryLogCount, _telemetryLogDropped;

    private void LogTelemetryState(string state, string detail)
    {
        lock (_log)
        {
            var minute = DateTime.Now; minute = new DateTime(minute.Year, minute.Month, minute.Day, minute.Hour, minute.Minute, 0);
            if (minute != _telemetryLogMinute)
            {
                if (_telemetryLogDropped > 0) _log.Write($"Web dashboard telemetry: {_telemetryLogDropped} more state changes not logged");
                _telemetryLogMinute = minute; _telemetryLogCount = 0; _telemetryLogDropped = 0;
            }
            if (++_telemetryLogCount > 20) { _telemetryLogDropped++; return; }
            _log.Write($"Web dashboard telemetry: {state} - {detail}");
        }
    }

    public bool Enabled
    {
        get => _settings.WebDashboardEnabled;
        set
        {
            if (!CanChange || value == Enabled) return;
            _settings.WebDashboardEnabled = value;
            _settings.Save();
            OnPropertyChanged();
            _ = ApplyAsync();
        }
    }

    public bool Network
    {
        get => _settings.WebDashboardNetwork;
        set
        {
            if (!CanChange || value == Network) return;
            _settings.WebDashboardNetwork = value;
            if (value && string.IsNullOrEmpty(_settings.WebDashboardToken)) _settings.WebDashboardToken = WebDashboardHost.NewNetworkToken();
            _settings.Save();
            OnPropertyChanged();
            _log.Write($"Web dashboard: network access {(value ? "on" : "off")}");
            if (Enabled) _ = ApplyAsync(restart: true);
        }
    }

    public string NetworkLink => _host.NetworkLinks.Count > 0 ? _host.NetworkLinks[0].AbsoluteUri : "";
    private string _qrFor = "";
    private System.Windows.Media.ImageSource? _qr;

    public System.Windows.Media.ImageSource? NetworkQr
    {
        get
        {
            string link = NetworkLink;
            if (link != _qrFor)
            {
                _qrFor = link;
                _qr = null;
                if (link.Length > 0)
                {
                    using var data = QRCoder.QRCodeGenerator.GenerateQrCode(link, QRCoder.QRCodeGenerator.ECCLevel.M);
                    byte[] png = new QRCoder.PngByteQRCode(data).GetGraphic(8);
                    var image = new System.Windows.Media.Imaging.BitmapImage();
                    image.BeginInit();
                    image.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad;
                    image.StreamSource = new MemoryStream(png);
                    image.EndInit();
                    image.Freeze();
                    _qr = image;
                }
            }
            return _qr;
        }
    }

    public string OtherNetworkLinks => _host.NetworkLinks.Count == 0 ? ""
        : $"Network: {_host.NetworkInterfaces[0]}." + (_host.NetworkLinks.Count > 1
            ? " If the phone cannot open it, it may be on another of this PC's networks: " +
              string.Join(", ", _host.NetworkLinks.Skip(1).Zip(_host.NetworkInterfaces.Skip(1), (u, n) => $"{u.Host} ({n})"))
            : "");
    public ICommand CopyLinkCommand { get; }
    public ICommand NewLinkCommand { get; }

    private void CopyLink()
    {
        try { System.Windows.Clipboard.SetText(NetworkLink); SetStatus("Network link copied."); }
        catch (System.Runtime.InteropServices.ExternalException e) { SetStatus($"The link could not be copied: {e.Message}"); }
    }

    private void NewLink()
    {
        _settings.WebDashboardToken = WebDashboardHost.NewNetworkToken();
        _settings.Save();
        _log.Write("Web dashboard: new network link made; earlier links no longer work");
        if (Enabled) _ = ApplyAsync(restart: true);
    }

    public bool CanChange => !_busy && !_closing;
    public bool IsRunning => _host.Address != null;
    public string Status => _status;
    public ICommand OpenCommand { get; }
    public ICommand RetryCommand { get; }

    public Task InitializeAsync() => Enabled ? ApplyAsync() : Task.CompletedTask;

    private async Task ApplyAsync(bool restart = false)
    {
        try
        {
            await _hostGate.WaitAsync(_lifetime.Token);
            try
            {
                if (_closing) return;
                _busy = true;
                SetStatus(Enabled ? "Starting local dashboard..." : "Stopping local dashboard...");
                if (Enabled)
                {
                    if (restart) await Task.Run(() => _host.DisposeAsync().AsTask());
                    _host.NetworkToken = Network ? _settings.WebDashboardToken : null;
                    await Task.Run(() => _host.StartAsync(_lifetime.Token));
                    SetStatus(Network
                        ? $"Running at {_host.Address}, and for other devices on the network through the link below."
                        : $"Running at {_host.Address}");
                }
                else
                {
                    await Task.Run(() => _host.DisposeAsync().AsTask());
                    SetStatus("Off.");
                }
            }
            finally
            {
                _busy = false;
                _hostGate.Release();
            }
        }
        catch (OperationCanceledException) when (_closing) { }
        catch (Exception e)
        {
            SetStatus($"Dashboard unavailable: {e.Message}");
            _log.Write($"Web dashboard failed: {e}");
        }
        finally
        {
            OnPropertyChanged(nameof(CanChange));
            OnPropertyChanged(nameof(IsRunning));
            OnPropertyChanged(nameof(NetworkLink));
            OnPropertyChanged(nameof(NetworkQr));
            OnPropertyChanged(nameof(OtherNetworkLinks));
            CommandManager.InvalidateRequerySuggested();
        }
    }

    public async Task StopAsync()
    {
        _closing = true;
        _lifetime.Cancel();
        await _hostGate.WaitAsync();
        try
        {
            await Task.Run(() => _host.DisposeAsync().AsTask());
            SetStatus("Stopped.");
        }
        catch (Exception e)
        {
            _log.Write($"Web dashboard shutdown failed: {e}");
        }
        finally
        {
            _hostGate.Release();
        }
    }

    private void SetStatus(string status)
    {
        _status = status;
        OnPropertyChanged(nameof(Status));
        OnPropertyChanged(nameof(CanChange));
        OnPropertyChanged(nameof(IsRunning));
        _log.Write($"Web dashboard: {status}");
    }

    private void OpenBrowser()
    {
        if (_host.Address is not { } address) return;
        try
        {
            Process.Start(new ProcessStartInfo(address.AbsoluteUri) { UseShellExecute = true });
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException)
        {
            SetStatus($"The browser could not be opened. Open {address} yourself. {e.Message}");
        }
    }
}
