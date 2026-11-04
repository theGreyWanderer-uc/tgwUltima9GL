namespace U9GL.Core.Install;

public sealed class GameFolder
{
    public const string BundleFolderName = "u9gl";

    public GameFolder(string root) => Root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));

    public static GameFolder ForLauncherDirectory(string launcherDirectory) =>
        new(Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(Path.GetFullPath(launcherDirectory)))
            ?? launcherDirectory);

    public string Root { get; }
    public string Exe => Path.Combine(Root, "u9.exe");
    public string OptionsIni => Path.Combine(Root, "Options.ini");
    public string Loader => Path.Combine(Root, "dsound.dll");
    public string Bundle => Path.Combine(Root, BundleFolderName);
    public string BundledLoader => Path.Combine(Bundle, "dsound.dll");
    public string OurDll => Path.Combine(Bundle, "u9log.dll");
    public string DefaultsIni => Path.Combine(Bundle, "u9gl.defaults.ini");
    public string U9glIni => Path.Combine(Bundle, "u9gl.ini");
    public string ProfileIni => Path.Combine(Bundle, "u9gl.profile.ini");
    public string DiagIni => Path.Combine(Bundle, "u9gl.diag.ini");
    public string Manifest => Path.Combine(Bundle, InstallManifest.FileName);
    public string BackupRoot => Path.Combine(Bundle, "backup");

    public string Relative(string path) => Path.GetRelativePath(Root, path);
    public string Absolute(string relativePath) => Path.GetFullPath(Path.Combine(Root, relativePath));

    public bool IsWritable()
    {
        string probe = Path.Combine(Root, ".u9gl-write-test-" + Guid.NewGuid().ToString("N"));
        try
        {
            File.WriteAllBytes(probe, []);
            File.Delete(probe);
            return true;
        }
        catch (Exception e) when (e is UnauthorizedAccessException or IOException)
        {
            return false;
        }
    }

    public string? VirtualStoreOptionsIni()
    {
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        string? root = Path.GetPathRoot(OptionsIni);
        if (string.IsNullOrEmpty(local) || string.IsNullOrEmpty(root)) return null;
        string redirected = Path.Combine(local, "VirtualStore", OptionsIni[root.Length..]);
        return File.Exists(redirected) ? redirected : null;
    }
}
