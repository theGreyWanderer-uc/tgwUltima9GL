using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;
using U9GL.Launcher.ViewModels;

namespace U9GL.Launcher.Converters;

public sealed class ToneBrushConverter : IValueConverter
{
    private static readonly Dictionary<Tone, Brush> Bright = new()
    {
        [Tone.Good] = Frozen(0xA6, 0xF5, 0x9E),
        [Tone.Warning] = Frozen(0xFF, 0xD9, 0x8A),
        [Tone.Bad] = Frozen(0xFF, 0xB3, 0xAC),
    };

    private static SolidColorBrush Frozen(byte r, byte g, byte b)
    {
        var brush = new SolidColorBrush(Color.FromRgb(r, g, b));
        brush.Freeze();
        return brush;
    }

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not Tone tone) return DependencyProperty.UnsetValue;
        if (parameter as string == "bright")
            return Bright.TryGetValue(tone, out var bright) ? bright : DependencyProperty.UnsetValue;

        string? key = tone switch
        {
            Tone.Good => "SystemFillColorSuccessBrush",
            Tone.Warning => "SystemFillColorCautionBrush",
            Tone.Bad => "SystemFillColorCriticalBrush",
            _ => null,
        };
        return key != null && Application.Current?.TryFindResource(key) is Brush brush ? brush : DependencyProperty.UnsetValue;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

public sealed class NullToVisibilityConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value == null ? Visibility.Collapsed : Visibility.Visible;

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

public sealed class SubtractConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is double d && double.TryParse(parameter as string, NumberStyles.Float, CultureInfo.InvariantCulture, out var less)
            ? Math.Max(0, d - less)
            : 0.0;

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

public sealed class FractionConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is double d && d > 0 &&
        double.TryParse(parameter as string, NumberStyles.Float, CultureInfo.InvariantCulture, out var f)
            ? Math.Floor(d * f)
            : double.NaN;

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

public sealed class AtLeastToVisibilityConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var text = parameter as string ?? "";
        var below = text.StartsWith('!');
        if (value is not double d ||
            !double.TryParse(below ? text[1..] : text, NumberStyles.Float, CultureInfo.InvariantCulture, out var limit))
            return Visibility.Collapsed;
        return (d >= limit) != below ? Visibility.Visible : Visibility.Collapsed;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

public sealed class StringToVisibilityConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        string.IsNullOrEmpty(value as string) ? Visibility.Collapsed : Visibility.Visible;

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
