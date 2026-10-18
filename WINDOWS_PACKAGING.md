# Windows Standalone Game Package

Run from the project root:

```powershell
.\Scripts\BuildWindows.ps1 -EngineRoot 'D:\APP\APP_Game\Unreal Engine\UE_5.7'
```

Builds the Shipping configuration with local UE 5.7, the Visual Studio C++ toolchain, and Windows SDK. Set the engine location with `UE_ROOT` if preferred; the script also attempts to read Epic's installation registry. First build the standalone Python runtime as described in [Runtime Deployment](RUNTIME_DEPLOYMENT.md).

Outputs:

- `dist/HandOffMyPlanet/Windows/VFX.exe`: double-click to launch.
- `dist/HandOffMyPlanet-Win64.zip`: complete distribution package.
- `dist/HandOffMyPlanet-Win64.zip.sha256`: archive SHA-256.
- `Saved/WindowsPackage/`: build logs, relocation test directories, and validation reports.

The build refuses to overwrite existing output. Specify a new `-OutputDirectory` when packaging again. `-SkipZip` generates only the complete directory; `-Configuration Development` creates a development/debug package.

## Contents and Isolation

`Config/DefaultGame.ini` specifies four maps (main menu, alien planet, FirstPerson, and Map), along with dynamically loaded UI, Creatures, Niagara, AlienPlanet, and ShieldHealth asset directories. Referenced materials, textures, audio, characters, and input assets are cooked together into compressed Pak / IoStore containers.

CosmoBridge and GestureBridge are runtime plugins. Editor-only tools are excluded from the runtime package. Python, locked dependencies, local hand/face models, tutorials, and service configuration are deployed through NonUFS, preserving directly accessible relative directories. Development virtual environments and external source directories are not copied.

UE rewrites all `.ini` files as engine configuration, including data files in Python dependencies. After archiving, the build script copies these files unchanged from the verified standalone runtime back into the final package, then performs a complete hash check. This step is also required when packaging directly from the editor; otherwise, runtime validation may fail.

The game locates its interpreter through the bundled plugin and uses the isolated `python312._pth`. Unreal Engine, Python, and Visual Studio do not need to be installed. The Visual C++ runtime installer is at `Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe`. Players need a graphics card and driver supporting the project's DirectX 12 / SM6 rendering configuration.

See the packaged `README.txt` for non-sensitive configuration, ports, and log locations. Players configure voice keys through their own Windows environment variables; keys are excluded from the distribution. Without a key, confirm the option to skip the tutorial for local exploration and combat.

## Validation

Before packaging, the build script checks the runtime. After packaging, it automatically runs:

```powershell
.\Scripts\VerifyWindowsPackage.ps1 -PackageRoot .\dist\HandOffMyPlanet\Windows
```

Verification copies the entire game to a new path containing Chinese characters and spaces, clears Python search paths, sets invalid `PYTHONHOME` / `PYTHONPATH` values, and checks:

1. Bundled files, runtime plugin configuration, system runtime installer, and Pak / IoStore containers are complete.
2. SHA-256 for all runtime files, local model inference, OpenCV, TLS certificates, and local HTTP interfaces.
3. Bundled service health readiness and cleanup after parent-process exit.
4. Actual packaged game startup, loading of the main menu, fonts, textures, nine tutorials, and dynamically loaded effects and models.
5. Skipping the tutorial through actual menu logic and switching to the planet; scene creatures have skeletons and animation instances, with minions, the boss, and non-hostile Crystal Eye present. Connects to the game's automatically started COSMO service and pauses/resumes. The report records the actual creature count; the currently saved level has five.
6. The game uses the relocated runtime and shuts down services it owns on exit.

Game validation is explicitly enabled with `-PackageSmokeTest -PackageSmokeReport=<path> -unattended`. The existing `-UIMenuServiceTest` switch allows the local service to start in unattended mode. Normal startup does not enter this path; unattended mode does not open the webcam, microphone, or real voice service. Validation requires port 8765 to be free. Reports are written to `Saved/WindowsPackage/Smoke-*.json`; success requires `ok=true` and `stage=2`.

`-SkipGameLaunch` checks only the runtime; `-SkipRelocation` checks the original package directory. Both reduce validation coverage. Target-machine graphics compatibility, human gestures, camera permissions, and Grok voice still require actual playtesting on the target hardware.
