using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.StaticFiles;

namespace U9GL.WebDashboard;

internal static class EmbeddedDashboardAssets
{
    internal static string FrontendBuild { get; } = ComputeBuild();

    private static string ComputeBuild()
    {
        using var index = typeof(EmbeddedDashboardAssets).Assembly.GetManifestResourceStream("frontend/index.html");
        return index == null ? "none" : Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(index))[..16];
    }

    internal static void MapDashboardAssets(this WebApplication application)
    {
        var assembly = typeof(EmbeddedDashboardAssets).Assembly;
        if (!assembly.GetManifestResourceNames().Contains("frontend/index.html", StringComparer.Ordinal))
            throw new InvalidOperationException("Web dashboard: embedded frontend index is missing.");
        var types = new FileExtensionContentTypeProvider();
        foreach (string resourceName in assembly.GetManifestResourceNames().Where(n => n.StartsWith("frontend/", StringComparison.Ordinal)))
        {
            string path = resourceName["frontend/".Length..].Replace('\\', '/');
            string route = path == "index.html" ? "/" : "/" + path;
            string contentType = types.TryGetContentType(path, out var detected) ? detected : "application/octet-stream";
            application.MapGet(route, (HttpContext context) =>
            {
                context.Response.Headers.CacheControl = path.StartsWith("assets/", StringComparison.Ordinal)
                    ? "public,max-age=31536000,immutable" : "no-cache";
                context.Response.Headers.XContentTypeOptions = "nosniff";
                return Results.Stream(assembly.GetManifestResourceStream(resourceName)!, contentType);
            });
        }
    }
}
