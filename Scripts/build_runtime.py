"""Build the Win64 redistributable from an audited, locked build environment.

Build-time Python may be a venv; the output never references or copies a venv.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata as metadata
import json
import shutil
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Plugins/CosmoBridge/Resources/Runtime/Win64'
PYTHON_URL = 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip'
PYTHON_SHA256 = '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
MODEL_HASHES = {
    'gesture_recognizer.task': '97952348cf6a6a4915c2ea1496b4b37ebabc50cbbf80571435643c455f2b0482',
    'face_landmarker.task': '64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff',
}


def sha256(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def copy_tree(source, target):
    shutil.copytree(source, target, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '.pytest_cache'))


def build(archive):
    if sys.platform != 'win32' or sys.version_info[:3] != (3, 12, 10):
        raise RuntimeError('Build with CPython 3.12.10 x64 and the runtime requirements lock.')
    if not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(PYTHON_URL, archive)
    if sha256(archive) != PYTHON_SHA256:
        raise RuntimeError('Portable Python archive checksum mismatch.')
    for name, digest in MODEL_HASHES.items():
        if sha256(ROOT / 'dubhack_gestures/models' / name) != digest:
            raise RuntimeError(f'Model checksum mismatch: {name}')
    requirements = []
    for line in (ROOT / 'Scripts/runtime-requirements.lock').read_text().splitlines():
        if line and not line.startswith('#'):
            name, version = line.split('==')
            dist = metadata.distribution(name)
            if dist.version != version:
                raise RuntimeError(f'Build dependency mismatch: {name} needs {version}')
            requirements.append(dist)
    # Never silently retain files from a previous dependency version.
    if OUTPUT.exists():
        raise RuntimeError(f'Output exists: {OUTPUT}. Archive or remove this exact directory before rebuilding.')
    python = OUTPUT / 'python'
    python.mkdir(parents=True)
    with zipfile.ZipFile(archive) as bundle:
        bundle.extractall(python)
    (python / 'python312._pth').write_text(
        'python312.zip\n.\nLib/site-packages\n../app/CosmoSystem\n../app/gestures\nimport site\n', encoding='utf-8')
    packages = python / 'Lib/site-packages'
    for dist in requirements:
        for relative in dist.files or []:
            path = Path(relative)
            if '..' in path.parts or path.suffix == '.pyc' or '__pycache__' in path.parts:
                continue
            if path.suffix == '.pth' or path.name.startswith('__editable__'):
                raise RuntimeError(f'Editable or external-path dependency rejected: {path}')
            source = Path(dist.locate_file(relative))
            if source.is_file():
                target = packages / path
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    app = OUTPUT / 'app'
    service = app / 'CosmoSystem'
    for directory in ('companion', 'data', 'static'):
        copy_tree(ROOT / 'External/CosmoSystem' / directory, service / directory)
    gestures = app / 'gestures'
    gestures.mkdir(parents=True)
    for name in ('gesture_recognition.py', 'gesture_logic.py', 'game_controls.py',
                 'interaction_controls.py', 'camera_preview.py', 'recognition_settings.json'):
        shutil.copy2(ROOT / 'dubhack_gestures' / name, gestures / name)
    copy_tree(ROOT / 'dubhack_gestures/models', gestures / 'models')
    shutil.copy2(ROOT / 'Plugins/GestureBridge/Resources/gesture_bindings.json', gestures)
    shutil.copy2(ROOT / 'Scripts/cosmo_service.py', app)
    shutil.copy2(ROOT / 'Scripts/runtime_check.py', app)
    shutil.copy2(ROOT / 'Scripts/runtime-defaults.json', OUTPUT / 'runtime.json')
    shutil.copy2(ROOT / 'Scripts/runtime-requirements.lock', OUTPUT)
    manifest = {
        'schema': 1, 'platform': 'Win64', 'python': '3.12.10',
        'python_source': PYTHON_URL, 'python_archive_sha256': PYTHON_SHA256,
        'dependencies': {dist.metadata['Name']: dist.version for dist in requirements},
        'files': {p.relative_to(OUTPUT).as_posix(): sha256(p)
                  for p in sorted(OUTPUT.rglob('*')) if p.is_file()},
    }
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'Built {len(manifest["files"])} files in {OUTPUT}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--python-archive', type=Path,
                        default=ROOT / 'Saved/RuntimeBuild/python-3.12.10-embed-amd64.zip')
    build(parser.parse_args().python_archive)
