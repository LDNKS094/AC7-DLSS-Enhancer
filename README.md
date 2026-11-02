# AC7 DLSS Enhancer

English | [中文](README_zh.md)

Adds DLSS Super Resolution, neural rendering, optional sharpening and NVIDIA frame generation to *Ace Combat 7*.

## Install

Requires Windows x64, an NVIDIA GPU and driver supporting the enabled features, and a separately installed x64 ASI Loader. UE4SS is not required. Existing ultrawide and force-feedback mods can remain installed.

Close the game and run `Install.cmd`. The installer finds the Steam game folder, or you can specify it:

```powershell
.\Install.cmd -GameDirectory 'F:\SteamLibrary\steamapps\common\ACE COMBAT 7'
```

Existing `AC7DLSS.ini` settings are preserved. On the first upgrade from the old version, the installer converts `AC7NativeModels.ini` settings and backs up the old ASI to avoid loading both versions. Legacy models, logs and captures remain untouched. Backups are kept in this package's `backups/` folder.

For manual installation:

| Package content | Game destination |
|---|---|
| `payload/AC7DLSS.asi` | Beside `Ace7Game.exe` |
| `payload/AC7DLSS/` | Same folder name, containing all runtime libraries |
| `presets/AC7DLSS.ini` | `AC7DLSS.ini` beside `Ace7Game.exe` |

When upgrading manually, move the old `AC7NativeModels.asi` out of the game folder and keep a backup. Do not load both versions. Keep your ASI Loader, UE4SS and other mods.

## Settings

Edit `AC7DLSS.ini` in the game folder and press **Ctrl+Shift+F11** to apply.

The normalized starting preset is [presets/AC7DLSS.ini](presets/AC7DLSS.ini). Updates do not overwrite your installed settings.

| Section | Settings |
|---|---|
| `SuperResolution` | DLSS toggle, quality mode and sharpening |
| `NeuralRendering` | Effect strength, preset and screen-edge optimization |
| `FrameGeneration` | Frame generation toggle and multiplier |

Disable the game's FXAA. `QualityMode` values 0/1/2 select Quality/Balanced/Performance.

`OptimizeEdges` reduces neural-rendering quality at the screen edges to improve performance. `CenterAspectRatio` sets the full-quality centre area, higher `EdgeQuality` retains more edge detail, and `BlendWidth` sets the transition width.

Frame generation uses `Multiplier=0` for Auto, `1` for no generated frames, and `2` or higher for a fixed total multiplier. Auto targets the game FPS cap, or display refresh when unlimited; VSync takes priority. Fixed multipliers retain the game's FPS cap. Requests above the supported maximum are capped. Unsupported dynamic Auto falls back to the highest fixed multiplier; unsupported FG is disabled.

With `Enabled=1`, frame generation follows the native game state automatically; a gameplay hotkey is not required. Recorded sessions confirm SR, NR, fixed 2x and Auto additional presentation. Automatic menu/pause transitions remain unconfirmed in those records; performance gains have not been measured.

| Shortcut | Action |
|---|---|
| Ctrl+Shift+F9 | Temporarily toggle the frame-generation request |
| Ctrl+Shift+F10 | Toggle SR/NR/sharpening effects; FG is separate |
| Ctrl+Shift+F11 | Reload settings |
| Ctrl+Shift+F12 | Save a before/after capture, excluding generated frames |

Logs are named `AC7DLSS-*.log`; captures go to `AC7DLSSCaptures/`. Unexpected runtime failures log a reason and terminate the game. Keep the corresponding log when reporting an issue.

## Uninstall

Close the game and run `Uninstall.cmd`, optionally with the same `-GameDirectory` argument. This removes the mod's ASI and eight runtime libraries while keeping settings, cache, logs, captures, legacy files and other mods.

## Build from source

Requires Visual Studio 2022 C++ tools, Windows SDK, CMake and the dependencies in `third_party/`. SDK binaries and models are not stored in Git; see [dependency notes](third_party/README.md).

```powershell
.\scripts\Build.ps1 -CMake 'cmake'
.\scripts\Package.ps1
```

Build outputs go to `payload/`. Installation ZIPs and SHA-256 files go to `releases/`. Neither script deploys files or starts the game.

`src/` contains production source, `presets/` configuration, `scripts/` release tools, and `licenses/` third-party notices. `development/` holds local development notes, history and captures and is excluded from installation packages. `build/` contains regenerable output only.
