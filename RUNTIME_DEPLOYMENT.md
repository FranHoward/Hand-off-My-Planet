# Bundled Windows Game Runtime

The game no longer uses the project's `.venv`, system Python, the `py` launcher, or development-machine directories at runtime.
UE locates `Resources/Runtime/Win64` through the CosmoBridge plugin. The editor and packaged game use the same directory structure.

## Deployed Resources

```text
Plugins/CosmoBridge/Resources/Runtime/Win64/
  python/                    CPython 3.12.10 x64 embedded, standard library, DLLs, dependencies, and licenses
    python.exe
    python312._pth           Points only to the local standard library, dependencies, and application
    Lib/site-packages/
  app/
    cosmo_service.py          UE-managed service entry point and parent-process exit monitor
    runtime_check.py          Offline integrity and inference checks
    CosmoSystem/
      companion/             HTTP, Grok, real-time voice, and tutorial logic
      data/                  Tutorial configuration and prompts
      static/                Debug web pages
    gestures/
      gesture_recognition.py
      gesture_logic.py
      game_controls.py
      interaction_controls.py
      camera_preview.py
      recognition_settings.json
      gesture_bindings.json
      models/
        gesture_recognizer.task
        face_landmarker.task
        MODEL_SOURCE.txt
  runtime.json               Non-sensitive service defaults
  runtime-requirements.lock   20 pinned runtime dependencies
  manifest.json              File SHA-256 hashes, Python source, and dependency versions
```

The runtime directory is approximately 329 MB. Dependencies include MediaPipe, OpenCV, NumPy, websockets, certifi, and their transitive dependencies. It excludes pip, pytest, virtual environments, development scripts, keys, and webcam images. Bundled Python and dependency licenses are retained. Model sources and hashes are in `MODEL_SOURCE.txt`.

Python comes from the [official Python embedded distribution](https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip), with the archive SHA-256 pinned to `4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3`. The interpreter uses a relative-path `_pth` to isolate itself from system `PYTHONHOME`, `PYTHONPATH`, and user package directories. Startup uses `-B`, avoiding bytecode writes to the installation directory.

## Game Packaging and Installation

Run `Scripts/BuildWindows.ps1 -EngineRoot '<UE-5.7-installation-directory>'` to generate a Windows Shipping game, ZIP, and SHA-256, and automatically validate the relocated game. See [Windows Standalone Packaging and Validation](WINDOWS_PACKAGING.md). The script also restores third-party Python `.ini` data files rewritten by UE's configuration filter. `CosmoBridge.Build.cs` registers every runtime file as an external **NonUFS** resource and automatically copies it to `VFX/Plugins/CosmoBridge/Resources/Runtime/Win64` in the package, outside Pak / IoStore so Python can access the files directly. GestureBridge also copies its binding configuration into the package.

Distribute the complete packaged directory rather than only the game executable. Do not separately copy `.venv`, `dubhack_gestures`, or `External/CosmoSystem`. Win64 builds fail explicitly if the runtime is missing, preventing a release that silently depends on the development machine. UE's Windows system runtime installation requirements continue to follow standard packaging configuration.

Gesture startup enforces `--require-local-models`. Missing models report a damaged installation instead of attempting a first-run network download. Models load from in-memory bytes, supporting installation paths containing Chinese characters and spaces. Development source retains manual model downloads; game startup does not use them.

## Configuration and Logs

- `runtime.json`: `provider=auto`, default voice model, and voice. Explicit environment variables take priority. Auto mode selects Grok when a key is available and mock otherwise.
- Grok real-time voice requires `XAI_API_KEY` (`GROK_API_KEY` is also supported). Optionally set `XAI_MODEL` to enable the HTTP text model. The service reads process or persistent Windows environment variables. Use `COSMO_AI_PROVIDER=mock` to force offline mode. Keep keys out of distribution directories.
- Gesture thresholds: `app/gestures/recognition_settings.json`; tutorials and prompts: `app/CosmoSystem/data/`. Rebuild the runtime after changing configuration or source files to update the integrity manifest.
- Ports remain HTTP 8765, real-time voice 8766, webcam preview 8767, and gesture UDP 5052.
- Editor logs: project `Saved/Logs/CosmoService.log` and `GestureRecognition.log`.
- Packaged game logs: `COSMO/Logs` under the current user's local application data directory, requiring no installation-directory write access.

## Verification

No system Python is required:

```powershell
./Plugins/CosmoBridge/Resources/Runtime/Win64/python/python.exe -B ./Plugins/CosmoBridge/Resources/Runtime/Win64/app/runtime_check.py
./Scripts/VerifyRuntimeDeployment.ps1
```

The first command checks all resource hashes, loads local hand/face models, runs inference on synthetic images, and checks OpenCV JPEG, TLS root certificates, HTTP health, and game events. The second copies the full runtime into a new path containing Chinese characters and spaces, clears Python search paths, sets invalid PYTHONHOME/PYTHONPATH values, then verifies the same flow and service shutdown cleanup. It does not access xAI or capture webcam/microphone input. Logs are saved under `Saved/RuntimeRelocation`.

Runtime tests do not replace complete game installation, camera permission, and human voice acceptance testing on another Windows machine.

## Rebuild the Runtime (Development / Build Machines Only)

Players do not need these steps. The build machine uses CPython **3.12.10 x64** with locked dependencies installed in a dedicated build environment:

```powershell
python -m pip install -r Scripts/runtime-requirements.lock
python Scripts/build_runtime.py
```

The script extracts files from distributions installed in the current build interpreter, verifies versions, retains DLLs/data/licenses, rejects editable installs and `.pth` entries pointing outside the environment, and does not copy the virtual environment. The Python embedded archive downloads to `Saved/RuntimeBuild`; use `--python-archive <archive-path>` for an already downloaded copy. Python and both models are checked against pinned SHA-256 hashes.

To prevent mixing old files into the new runtime, the builder refuses to overwrite existing output. Before rebuilding, move the exact old directory `Plugins/CosmoBridge/Resources/Runtime/Win64` into `Saved/RuntimeBuild` as a backup. Then generate the new directory, run relocation verification, and rebuild the game. Backups and test copies under `Saved` are excluded from the game package.
