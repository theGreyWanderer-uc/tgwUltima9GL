# U9GL

An OpenGL renderer, launcher and fixes for **Ultima IX: Ascension** (the GOG and Steam releases).

U9GL adds a fourth renderer to the game, alongside its DirectX 6, DirectX 7 and Glide ones.
It also fixes crashes and bugs in the original game and adds optional improvements, all
switched in its launcher.

**Downloads are on the [Releases](../../releases) page.** This repository holds U9GL's
source; you do not need to build it to play.

## What you need

- Ultima IX: Ascension, the **GOG** or **Steam** release. Both ship the same game program, and
  the launcher identifies it by its files and says plainly if it does not recognise yours.
- Windows, 64-bit. The launcher brings its own copy of .NET, so nothing else needs to be
  installed.

**No game files are included.** U9GL needs your own copy of the game.

## Install

1. Download `u9gl-<version>.zip` from [Releases](../../releases).
2. Unzip it into the game folder, so that the `u9gl` folder sits beside `u9.exe`:

   ```
   Ultima9\u9.exe
   Ultima9\u9gl\U9GL.exe
   ```

3. Run `u9gl\U9GL.exe`. Its dashboard checks the game and offers **Install**, which puts one
   file, `dsound.dll`, beside `u9.exe`. Anything it replaces or edits is backed up in
   `u9gl\backup\` first.
4. Start the game with **Play**, or the way you always do. Settings are in `U9GL.exe`.

To check a download, compare its SHA-256 with the `.sha256` file beside it on the release
page: in PowerShell, `Get-FileHash u9gl-<version>.zip`.

## Update

Unzip the new release over the old `u9gl` folder, run `U9GL.exe` and choose **Update** on its
dashboard. Your settings are kept.

## Uninstall

**Uninstall** in the dashboard, then delete the `u9gl` folder. Uninstall puts back every
game setting U9GL changed.

## Reporting a problem

Open an [issue](../../issues) with:

- the U9GL version (on the launcher's About page);
- what happened and what you were doing;
- the log, `%TEMP%\u9log.txt`, and any crash report in `%TEMP%\u9gl_diag\`.

## Building from source

U9GL works against one build of the game, the `u9.exe` that GOG and Steam both ship (Steam's
differs only by a signature): the native code patches it at fixed places and checks the bytes
it expects before touching anything.

You need, on Windows:

- **Git Bash** (with Git for Windows) or MSYS2, to run the build script;
- **MSYS2's 32-bit MinGW GCC** (`mingw-w64-i686-gcc`), found at
  `C:\msys64\mingw32\bin\gcc.exe`, or give the path to `build.sh`;
- **Python 3**, which the build uses to check the settings files;
- the **.NET 10 SDK** and **Node.js 22.13 or newer**, for the launcher and its web
  dashboard.

```
cd u9gl
./build.sh                              # u9log.dll, dsound.dll, u9inject.exe, u9sample.exe into u9gl/out/
cd ..
dotnet build launcher/U9GL.slnx -c Release
dotnet test  launcher/U9GL.slnx -c Release
```

What is where:

| Folder | What |
|---|---|
| `u9gl/` | the native part: `dsound.dll`, the loader the game picks up, and `u9log.dll`, the renderer, fixes and console. `u9gl.ini` is the settings guide players get |
| `launcher/` | `U9GL.exe`: install, settings, saves, and the web dashboard |
| `include/` | the renderer interface header; the build compiles its copy in `u9gl/include/` and stops if the two differ |
| `tests/` | native tests: calling conventions, telemetry |
| `tools/` | packaging, and the checks the build runs |

## Licence

U9GL is **source-available**, under the U9GL Non-Commercial Copyleft Licence 1.0
([`LICENSE.md`](LICENSE.md)) - not an OSI-approved open-source licence. In short:

- **Personal, non-commercial use is free:** play with it, build it, change it, and share
  your changes, as long as whoever gets a copy also gets its source and this licence.
- **Organisations and commercial use need a paid licence first**, internal use included:
  see [`U9GL-COMMERCIAL-LICENSING.md`](U9GL-COMMERCIAL-LICENSING.md).
- [`LICENSING-FAQ.md`](LICENSING-FAQ.md) answers the common questions; the licence itself
  decides where they differ.

Contributions need a signed contributor licence agreement before they are merged:
[`CONTRIBUTING.md`](CONTRIBUTING.md).

**Licensing contact: u9gl-license@proton.me** - for commercial licences, contributor
agreements and licensing questions. The licensing documents refer to this address.

Third-party parts keep their own licences, in the files beside them: `stb_image.h` (public
domain or MIT), QRCoder (MIT), React (MIT), the Antonio font (SIL Open Font License 1.1) and
trekcolors (MIT). The bundled .NET runtime is Microsoft's, under its own licence:
`u9gl\runtime\LICENSE.txt` and `ThirdPartyNotices.txt`.

## Not affiliated

U9GL is a fan project. It is not affiliated with or endorsed by Electronic Arts, Origin
Systems, GOG or Valve. Ultima is a trademark of Electronic Arts.

## Generative AI Disclosure

Generative artificial intelligence (AI) tools have been used to assist in the development of U9GL.

AI assistance may include code generation, code review, debugging, documentation, technical research, and implementation suggestions.

The project is developed and maintained by its author, who is responsible for reviewing, testing, integrating, and maintaining the resulting code.

AI-generated suggestions are not automatically accepted and are subject to the same development and validation processes as other contributions.

The use of AI assistance does not imply endorsement, affiliation, or ownership of U9GL by any AI service provider.
