# Hand off My Planet

Unreal Engine 5.7 game with hand and face gesture controls and the COSMO AI companion.

## Development setup

Use Windows, Unreal Engine 5.7, the Visual Studio C++ tools required by Unreal, and CPython 3.12.10 x64.

The generated Python runtime is excluded from Git. Before opening or building a fresh clone, create it from the checked-in source and dependency lock:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r Scripts/runtime-requirements.lock
.\.venv\Scripts\python.exe Scripts/build_runtime.py
```

Open `VFX.uproject`, allow Unreal to build the project plugins, then press Play. The default map is the main menu. See [runtime deployment](RUNTIME_DEPLOYMENT.md) for packaging, runtime verification and rebuild instructions.

COSMO uses the local mock provider when no API key is configured. To enable Grok, set `XAI_API_KEY` and `XAI_MODEL` in the environment inherited by Unreal. Keep API keys out of project files.

## Project guides

- [Gesture and face controls](GESTURE_ONLY.md)
- [COSMO integration and voice](COSMO_INTEGRATION.md)
- [AI character setup](BP_FIRST_PERSON_AI.md)
- [Alien planet level](ALIEN_PLANET_LEVEL.md)
- [Creature animations](CREATURE_ANIMATIONS.md)

## Repository contents

Game assets and maps in `Content`, plugin source, configuration, original art in `External`, gesture models and build scripts are included. `.gitignore` excludes generated binaries, Unreal caches, logs, autosaves, test backups, virtual environments and the reproducible Python runtime. Ignored files remain on the local machine.
