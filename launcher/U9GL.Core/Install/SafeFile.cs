namespace U9GL.Core.Install;

internal static class SafeFile
{
    public const string TempSuffix = ".u9gl-tmp";

    public static void WriteAllBytes(string path, byte[] bytes)
    {
        string temp = path + TempSuffix;
        File.WriteAllBytes(temp, bytes);
        File.Move(temp, path, overwrite: true);
    }

    public static void Copy(string source, string destination)
    {
        string temp = destination + TempSuffix;
        File.Copy(source, temp, overwrite: true);
        File.Move(temp, destination, overwrite: true);
    }
}
