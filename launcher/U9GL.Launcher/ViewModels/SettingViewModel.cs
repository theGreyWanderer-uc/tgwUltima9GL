using System.Collections.ObjectModel;
using System.Windows.Input;
using U9GL.Core.Install;
using U9GL.Core.Settings;

namespace U9GL.Launcher.ViewModels;

public sealed class ChoiceViewModel(SettingChoice choice)
{
    public SettingChoice Choice { get; } = choice;
    public string Label => Choice.Label;
    public string Note => Choice.Note;
    public LedgerStatus Status => Choice.Status;
    public bool IsVerified => Choice.Status is LedgerStatus.Works or LedgerStatus.Partial;

    public string StatusText => Choice.Status switch
    {
        LedgerStatus.Works => "verified",
        LedgerStatus.Partial => "works, with a known defect",
        LedgerStatus.Untested => "untested",
        _ => "broken",
    };
}

public sealed class CustomValueViewModel(SettingKey key) : ObservableObject
{
    private string _text = "";

    public SettingKey Key { get; } = key;

    public string Label => Key.Key switch
    {
        "GameScreenWidth" => "Width",
        "GameScreenHeight" => "Height",
        _ => Key.Key,
    };

    public string Text
    {
        get => _text;
        set => SetProperty(ref _text, value);
    }
}

public sealed class SettingViewModel : ObservableObject
{
    private readonly Action _changed;
    private CurrentSetting? _current;
    private ChoiceViewModel? _selected;
    private Dictionary<string, string>? _custom;
    private string _customError = "";

    public SettingViewModel(SettingDefinition definition, Action changed)
    {
        Definition = definition;
        _changed = changed;
        Choices = definition.Choices
            .Where(c => c.Status != LedgerStatus.Broken)
            .Select(c => new ChoiceViewModel(c))
            .ToList();
        PageChoices = Choices.Where(c => c.IsVerified).ToList();
        CustomValues = new ObservableCollection<CustomValueViewModel>(definition.Keys.Select(k => new CustomValueViewModel(k)));
        ApplyCustomCommand = new RelayCommand(ApplyCustom, () => AllowsCustom);
        TurnOnCommand = new RelayCommand(() => Selected = Choices.FirstOrDefault(c => c.Label == Definition.DefaultLabel));
        UseVerifiedCommand = new RelayCommand(() => Selected = PageChoices.FirstOrDefault(), () => CanUseVerified);
    }

    public SettingDefinition Definition { get; }
    public string Title => Definition.Title;
    public string Help => Definition.Help;
    public IReadOnlyList<ChoiceViewModel> Choices { get; }
    public IReadOnlyList<ChoiceViewModel> PageChoices { get; }
    public ObservableCollection<CustomValueViewModel> CustomValues { get; }
    public bool AllowsCustom => Definition.CustomStatus != null;
    public bool PageAllowsCustom => Definition.CustomStatus is LedgerStatus.Works or LedgerStatus.Partial;
    public ICommand ApplyCustomCommand { get; }
    public ICommand TurnOnCommand { get; }
    public ICommand UseVerifiedCommand { get; }

    public ChoiceViewModel? Selected
    {
        get => _selected;
        set
        {
            if (!SetProperty(ref _selected, value)) return;
            if (value != null) _custom = null;
            Changed();
        }
    }

    public ChoiceViewModel? PageSelected
    {
        get => _selected is { IsVerified: true } ? _selected : null;
        set { if (value != null) Selected = value; }
    }

    public bool HasChoices => PageChoices.Count > 1;

    public bool IsSingle => PageChoices.Count == 1;

    public string SingleValueText => IsSingle ? PageChoices[0].Label : "";

    public string SingleNote => !IsSingle || Definition.OnlyValue ? "" : Definition.HasExperimental
        ? "The only value verified so far. Others can be tried on the Experimental page."
        : "The only value verified so far.";

    public bool CanUseVerified =>
        IsSingle && (_custom != null || _selected is { IsVerified: false } || (_selected == null && _current is { IsSet: true }));

    public string CustomError
    {
        get => _customError;
        private set => SetProperty(ref _customError, value);
    }

    public bool IsChanged =>
        _current != null &&
        Definition.Keys.Any(k => Pending(k.Key) is { } value && value != _current.Values.GetValueOrDefault(k.Key));

    public IEnumerable<SettingWrite> Writes() =>
        Definition.Keys
            .Select(k => (Key: k, Value: Pending(k.Key)))
            .Where(p => p.Value != null)
            .Select(p => new SettingWrite(p.Key.File, p.Key.Section, p.Key.Key, p.Value!));

    public string DiagNote =>
        _current is { Overridden.Count: > 0 } c && Definition.Page != SettingPage.Diagnostics
            ? $"Overridden: u9gl\\u9gl.diag.ini sets {string.Join(", ", c.Overridden.Select(k => $"{k} = {c.Values.GetValueOrDefault(k)}"))}, "
              + "and it wins over this page. Saving a change here takes that line out."
            : "";

    public bool IsOverridden => _current is { Overridden.Count: > 0 };

    public string PageNote
    {
        get
        {
            if (PageAllowsCustom && _custom != null)
                return $"A custom value: {PendingText}.";
            if (PageAllowsCustom && _selected == null && _current is { IsSet: true })
                return $"A custom value: {ValuesText(_current.Values)}.";
            if (_custom != null || _selected is { IsVerified: false })
                return $"Set to {PendingText} on the Experimental page (untested).";
            if (_selected == null && _current is { IsSet: true })
                return $"Currently {ValuesText(_current.Values)}, which is not a verified value.";
            if (_selected == null)
                return "Not set yet: Install writes the verified value.";
            return _selected.Note.Length > 0 ? _selected.Note : "";
        }
    }

    public bool IsOn => Pending(Definition.Keys[0].Key) is null or "1";

    public bool CanTurnOn => !IsOn;

    public string FixStateText
    {
        get
        {
            if (!IsOn)
            {
                var off = Definition.Choices.FirstOrDefault(c => c.Status == LedgerStatus.Broken);
                return off is null || off.Note.Length == 0 || off.Note == SettingsCatalog.StockCrashReturns
                    ? "Off. The crash this fixes can happen again."
                    : $"Off: {off.Note}.";
            }
            var on = Choices.FirstOrDefault(c => c.Label == Definition.DefaultLabel);
            return on is { Status: LedgerStatus.Partial } ? $"On. Works, with a known defect: {on.Note}." : "On";
        }
    }

    public Tone FixTone =>
        !IsOn ? Tone.Bad :
        Choices.Any(c => c.Label == Definition.DefaultLabel && c.Status == LedgerStatus.Partial) ? Tone.Warning : Tone.Good;

    public bool IsSwitchOn
    {
        get
        {
            string key = Definition.Keys[0].Key;
            string off = Definition.Choices.First(c => c.Label == "Off").Values[key];
            string? value = Pending(key) ?? Definition.Default.Values[key];
            return value != off;
        }
        set
        {
            if (value == IsSwitchOn) return;
            Selected = Choices.FirstOrDefault(c => c.Label == (value ? "On" : "Off"));
        }
    }

    public void Load(CurrentSetting current)
    {
        _current = current;
        _custom = null;
        _customError = "";
        _selected = current.Choice == null ? null : Choices.FirstOrDefault(c => c.Choice == current.Choice);
        foreach (var box in CustomValues)
            box.Text = current.Values.GetValueOrDefault(box.Key.Key) ?? "";
        OnPropertyChanged(string.Empty);
    }

    private void ApplyCustom()
    {
        var values = new Dictionary<string, string>();
        foreach (var box in CustomValues)
        {
            if (!int.TryParse(box.Text.Trim(), out int number) || number < 320 || number > 7680)
            {
                CustomError = $"{box.Label} must be a whole number from 320 to 7680.";
                return;
            }
            values[box.Key.Key] = number.ToString();
        }
        CustomError = "";

        var listed = Choices.FirstOrDefault(c => c.Choice.Values.All(p => values.GetValueOrDefault(p.Key) == p.Value));
        if (listed != null)
        {
            Selected = listed;
            return;
        }
        _selected = null;
        _custom = values;
        Changed();
    }

    private string? Pending(string key) =>
        _custom != null ? _custom.GetValueOrDefault(key) :
        _selected != null ? _selected.Choice.Values.GetValueOrDefault(key) :
        _current?.Values.GetValueOrDefault(key);

    private string PendingText =>
        _selected?.Label ?? ValuesText(Definition.Keys.ToDictionary(k => k.Key, k => Pending(k.Key)));

    private static string ValuesText(IReadOnlyDictionary<string, string?> values) =>
        string.Join(" × ", values.Values.Select(v => v ?? "?"));

    private void Changed()
    {
        OnPropertyChanged(string.Empty);
        _changed();
    }
}
