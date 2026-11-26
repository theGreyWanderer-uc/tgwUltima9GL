using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Input;
using U9GL.Core.Install;
using U9GL.Core.Saves;
using U9GL.Launcher.Services;

namespace U9GL.Launcher.ViewModels;

public sealed class NewGameSnapshotViewModel : ObservableObject
{
    private readonly DataPage _page;
    private bool _busy;
    private string _resultText = "";
    private Tone _resultTone = Tone.Neutral;

    public NewGameSnapshotViewModel(DataPage page)
    {
        _page = page;
        MakeCommand = new RelayCommand(() => _ = MakeAsync(), () => !_busy && !Exists);
    }

    private MainViewModel Main => _page.Main;
    private string Root => Main.Folder.Root;

    public string HoldingDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "U9GL", "New-game snapshot runs");

    public ICommand MakeCommand { get; }

    public bool Exists => NewGameSnapshot.Exists(Root);
    public bool Missing => !Exists;

    public string PresentText
    {
        get
        {
            if (!Exists) return "";
            var made = File.GetLastWriteTime(NewGameSnapshot.SnapshotPath(Root));
            return $"Present: static\\u9game0.sav, made {DataPage.When(made)}. The journal's New Game starts from it, "
                   + "with the toolbelt, backpack and journal in the house.";
        }
    }

    public bool IsBusy
    {
        get => _busy;
        private set { if (SetProperty(ref _busy, value)) CommandManager.InvalidateRequerySuggested(); }
    }

    public string ResultText { get => _resultText; private set => SetProperty(ref _resultText, value); }
    public Tone ResultTone { get => _resultTone; private set => SetProperty(ref _resultTone, value); }

    private void Say(string text, Tone tone = Tone.Neutral)
    {
        ResultText = text;
        ResultTone = tone;
    }

    public void Refresh()
    {
        OnPropertyChanged(nameof(Exists));
        OnPropertyChanged(nameof(Missing));
        OnPropertyChanged(nameof(PresentText));
        _page.Recheck();
        CommandManager.InvalidateRequerySuggested();
    }

    public void RestoreLeftovers()
    {
        if (Main.GameRunning) return;
        foreach (string folder in NewGameSnapshot.Unrestored(HoldingDirectory, Root))
        {
            try
            {
                NewGameSnapshot.Restore(folder);
                Main.Log.Write($"New-game snapshot: put back the save files an unfinished run had set aside in {folder}");
                Say($"An earlier snapshot run did not finish. Your save files were put back from {folder}.", Tone.Warning);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or SnapshotRunException)
            {
                Main.Log.Write($"New-game snapshot: restoring {folder} failed: {e.Message}");
                Say($"An earlier snapshot run did not finish, and your save files could not be put back: {e.Message} "
                    + $"They are in {Path.Combine(folder, "working")}; copy them into the savegame folder.", Tone.Bad);
            }
        }
    }

    private async Task MakeAsync()
    {
        if (IsBusy || Exists) return;
        IsBusy = true;
        try
        {
            await RunAsync();
        }
        finally
        {
            IsBusy = false;
            Refresh();
            Main.Refresh();
        }
    }

    private async Task RunAsync()
    {
        Main.Refresh();
        if (Main.GameRunning) { Say("Ultima IX is running. Quit the game first.", Tone.Warning); return; }
        if (!Main.CanStartGame) { Say("U9GL has to be installed and the game startable from Play first. See the Dashboard.", Tone.Warning); return; }

        Say("Checking the game's data files...");
        while (Main.GameData.IsBusy) await Task.Delay(250);
        await Main.GameData.CheckOnStartAsync();
        if (Main.GameData.NotStockReason is { } reason)
        {
            Say("The new-game snapshot has to be made on the game's own data: it copies the installed files, and "
                + "fan patches change them. Make the snapshot before installing fan patches, or remove them first. " + reason,
                Tone.Warning);
            Main.Log.Write($"New-game snapshot: refused, the data is not stock ({reason})");
            return;
        }

        if (!Dialogs.Confirm(
                "U9GL will start Ultima IX on a new game and let the opening play. At the first hint ankh it saves the "
                + "new-game snapshot and closes the game by itself. It takes about 2 minutes.\n\n"
                + "Don't touch anything while it runs.\n\n"
                + $"Your current game is not touched: start.dat and the working files in savegame are set aside in {HoldingDirectory} "
                + "for the run and put back after.\n\nStart now?"))
            return;

        string bundle = Main.Folder.Bundle;
        string request = Path.Combine(bundle, NewGameSnapshot.RequestName);
        string result = Path.Combine(bundle, NewGameSnapshot.ResultName);
        string holding;
        try
        {
            if (File.Exists(result)) File.Delete(result);
            holding = NewGameSnapshot.SetAside(Root, HoldingDirectory);
            Main.Log.Write($"New-game snapshot: save files set aside in {holding}");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or SnapshotRunException)
        {
            Say($"The save files could not be set aside, so the game was not started: {e.Message}", Tone.Bad);
            Main.Log.Write($"New-game snapshot: setting aside failed: {e.Message}");
            return;
        }

        Main.SetBusy(true);
        try
        {
            File.WriteAllText(request, "Written by the launcher: the next launch makes static\\u9game0.sav (I-32). U9GL deletes this file.\r\n");
            Say("The game is running: U9GL is making the snapshot. Don't touch anything; the game closes itself.");
            await PlayAndWaitAsync();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or Win32Exception or InvalidDataException)
        {
            Say($"The game could not be started: {e.Message}", Tone.Bad);
            Main.Log.Write($"New-game snapshot: start failed: {e.Message}");
        }
        finally
        {
            Main.SetBusy(false);
            try { if (File.Exists(request)) File.Delete(request); } catch (IOException) { }
            Restore(holding);
        }
        if (ResultTone == Tone.Bad) return;
        Report(NewGameSnapshot.ReadResult(bundle));
    }

    private async Task PlayAndWaitAsync()
    {
        bool largeAddress = Main.StartsLargeAddress;
        using Process? game = GameLauncher.Start(Main.Folder, largeAddress);
        Main.Log.Write($"New-game snapshot: started {(largeAddress ? LargeAddress.CopyPath(Main.Folder) : Main.Folder.Exe)}");
        if (game != null)
        {
            GameWindow.AllowForeground(game.Id);
            await game.WaitForExitAsync();
        }
        var probe = new GameProcessProbe();
        await Task.Delay(1000);
        while (probe.IsRunning(Main.Folder)) await Task.Delay(1000);
        Main.Log.Write("New-game snapshot: the game has closed");
    }

    private void Restore(string holding)
    {
        try
        {
            NewGameSnapshot.Restore(holding);
            Main.Log.Write($"New-game snapshot: save files put back from {holding}");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or SnapshotRunException)
        {
            Main.Log.Write($"New-game snapshot: putting the save files back failed: {e.Message}");
            Say($"Your save files could not be put back: {e.Message} They are in {Path.Combine(holding, "working")}; "
                + "the launcher tries again when it next starts.", Tone.Bad);
        }
    }

    private void Report(SnapshotCheck? game)
    {
        if (!Exists)
        {
            string why = game is { Ok: false, Text.Length: > 0 } ? game.Text
                       : game == null ? "The game closed before U9GL said anything: it may have been closed by hand, or crashed."
                       : "No snapshot was written.";
            Say($"No snapshot was made. {why} Your save files are back as they were.", Tone.Bad);
            Main.Log.Write($"New-game snapshot: none made ({why})");
            return;
        }
        var check = NewGameSnapshot.Check(Root);
        if (!check.Ok)
        {
            string path = NewGameSnapshot.SnapshotPath(Root);
            try
            {
                File.SetAttributes(path, FileAttributes.Normal);
                File.Delete(path);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { Main.Log.Write($"New-game snapshot: removing the bad one failed: {e.Message}"); }
            Say($"The snapshot did not pass the check and was removed: {check.Text} Your save files are back as they were.", Tone.Bad);
            Main.Log.Write($"New-game snapshot: made but failed the check ({check.Text})");
            return;
        }
        Say($"New-game snapshot made. {check.Text} Your save files are back as they were.", Tone.Good);
        Main.Log.Write($"New-game snapshot: made and checked ({check.Text})");
    }
}
