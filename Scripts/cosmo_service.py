"""UE-owned COSMO entry point. Credentials are inherited, never passed as arguments."""
from pathlib import Path
import os
import sys
import threading
import ctypes
import argparse
import json


def watch_parent(pid):
    """Exit if the owning UE process crashes, even when EndPlay cannot run."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
    if not handle:
        os._exit(1)
    kernel.WaitForSingleObject(handle, 0xFFFFFFFF)
    kernel.CloseHandle(handle)
    os._exit(0)


def refresh_environment():
    """Editors launched before setx still see current persistent Windows settings."""
    names = ('XAI_API_KEY', 'GROK_API_KEY', 'XAI_MODEL', 'COSMO_AI_PROVIDER', 'XAI_VOICE_MODEL', 'XAI_VOICE')
    if sys.platform == 'win32':
        import winreg
        for name in names:
            if os.environ.get(name, '').strip():
                continue
            for hive, path in ((winreg.HKEY_CURRENT_USER, 'Environment'),
                               (winreg.HKEY_LOCAL_MACHINE, r'SYSTEM\CurrentControlSet\Control\Session Manager\Environment')):
                try:
                    with winreg.OpenKey(hive, path) as key:
                        value, _ = winreg.QueryValueEx(key, name)
                    if isinstance(value, str) and value.strip():
                        os.environ[name] = value.strip()
                        break
                except OSError:
                    pass
    if not os.environ.get('XAI_API_KEY', '').strip() and os.environ.get('GROK_API_KEY', '').strip():
        os.environ['XAI_API_KEY'] = os.environ['GROK_API_KEY'].strip()


def runtime_modes(config):
    requested = os.environ.get('COSMO_AI_PROVIDER', '').strip().lower() or config.get('provider', 'auto')
    key_ready = bool(os.environ.get('XAI_API_KEY', '').strip())
    voice_enabled = key_ready and requested != 'mock'
    provider = requested
    if provider == 'auto':
        provider = 'grok' if key_ready and os.environ.get('XAI_MODEL', '').strip() else 'mock'
    if provider in ('grok', 'xai') and not os.environ.get('XAI_MODEL', '').strip():
        provider = 'mock'
    return provider, voice_enabled, key_ready


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('legacy_parent_pid', nargs='?', type=int)
    parser.add_argument('--parent-pid', type=int)
    parser.add_argument('--log-dir', type=Path)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--voice-port', type=int, default=8766)
    args = parser.parse_args()
    parent = args.parent_pid or args.legacy_parent_pid
    if parent:
        threading.Thread(target=watch_parent, args=(parent,), daemon=True).start()
    refresh_environment()
    root = Path(__file__).resolve().parents[1]
    if not (root / 'runtime.json').is_file():
        raise RuntimeError('Run the bundled app/cosmo_service.py using the bundled python/python.exe.')
    config = json.loads((root / 'runtime.json').read_text(encoding='utf-8'))
    log_dir = args.log_dir or Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'COSMO/Logs'
    log_dir.mkdir(parents=True, exist_ok=True)
    log = log_dir / 'CosmoService.log'
    with log.open('w', encoding='utf-8', buffering=1) as output:
        sys.stdout = sys.stderr = output
        try:
            service = root / 'app' / 'CosmoSystem'
            sys.path.insert(0, str(service))
            os.chdir(service)
            provider, voice_enabled, key_ready = runtime_modes(config)
            os.environ['COSMO_AI_PROVIDER'] = provider
            print(f'COSMO text provider: {provider}. Realtime voice enabled: {voice_enabled}.')
            if not key_ready:
                print('Grok voice unavailable: XAI_API_KEY is not configured for this process or persistent Windows environment.')
            os.environ.setdefault('XAI_VOICE_MODEL', config['voice_model'])
            os.environ.setdefault('XAI_VOICE', config['voice'])
            import certifi
            # Embedded Python carries its own CA roots in addition to Windows trust.
            os.environ.setdefault('SSL_CERT_FILE', certifi.where())
            from companion.http_api import serve
            serve(args.host, args.port, args.voice_port if voice_enabled else 0)
        except Exception:
            # Avoid exception payloads, which may contain upstream credentials.
            print('COSMO startup failed; check Python dependencies, provider configuration and local ports.')
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
