using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Effects;

namespace U9GL.Launcher.Effects;

public sealed class OnWhiteToAlpha : ShaderEffect
{
    private static readonly PixelShader Shader = new()
    {
        UriSource = new Uri("pack://application:,,,/Effects/OnWhiteToAlpha.ps"),
    };

    public static readonly DependencyProperty InputProperty =
        RegisterPixelShaderSamplerProperty(nameof(Input), typeof(OnWhiteToAlpha), 0);

    public OnWhiteToAlpha()
    {
        PixelShader = Shader;
        UpdateShaderValue(InputProperty);
    }

    public Brush Input
    {
        get => (Brush)GetValue(InputProperty);
        set => SetValue(InputProperty, value);
    }
}
