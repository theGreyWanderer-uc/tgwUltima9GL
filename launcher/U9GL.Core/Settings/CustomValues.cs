namespace U9GL.Core.Settings;

public static class CustomValues
{
    public const int AvatarNameMax = 24;

    public static string Label(string key) => key switch
    {
        "GameScreenWidth" => "Width",
        "GameScreenHeight" => "Height",
        "avatar_name" => "Name",
        _ => key,
    };

    public static string Prompt(string key) => key == "avatar_name" ? "Or your own name:" : "Or a custom value:";

    public static (string Label, string Note) AsChoice(string key) => key switch
    {
        "avatar_name" => ("Your own name",
            $"Up to {AvatarNameMax} characters: letters, spaces, apostrophes, hyphens and full stops."),
        "GameScreenWidth" or "GameScreenHeight" => ("A custom size", "Any width and height from 320 to 7680."),
        _ => ("A custom value", ""),
    };

    public static bool IsText(string key) => key == "avatar_name";

    public static string? Check(string key, string text, out string value)
    {
        value = text.Trim();
        switch (key)
        {
            case "GameScreenWidth":
            case "GameScreenHeight":
                if (!int.TryParse(value, out int number) || number < 320 || number > 7680)
                    return $"{Label(key)} must be a whole number from 320 to 7680.";
                value = number.ToString();
                return null;

            case "avatar_name":
                return CheckName(value);

            default:
                return null;
        }
    }

    private static string? CheckName(string name)
    {
        if (name.Length == 0)
            return "Type a name.";
        if (name.Length > AvatarNameMax)
            return $"A name can be at most {AvatarNameMax} characters.";
        if (!char.IsLetter(name[0]))
            return "A name must start with a letter.";
        foreach (char c in name)
        {
            if (c > 'ÿ')
                return $"'{c}' cannot be shown by the game's text.";
            if (!(char.IsLetter(c) || c is ' ' or '\'' or '-' or '.'))
                return "Use letters, spaces, apostrophes, hyphens and full stops only.";
        }
        if (name.Contains("  "))
            return "Use single spaces between words.";
        return null;
    }
}
