"""Offline verification using only the deployed interpreter and resources."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import ssl
import sys
import threading
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-inference', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    for relative, expected in manifest['files'].items():
        with (root / relative).open('rb') as source:
            actual = hashlib.file_digest(source, 'sha256').hexdigest()
        if actual != expected:
            raise RuntimeError(f'Runtime integrity failure: {relative}')
    assert Path(sys.executable).resolve() == root / 'python/python.exe'
    assert sys.prefix == sys.base_prefix, 'Unexpected virtual environment'
    assert all(Path(p).resolve().is_relative_to(root) for p in sys.path), sys.path
    import certifi
    import cv2
    import mediapipe as mp
    import numpy as np
    import websockets
    from mediapipe.tasks import python
    from mediapipe.tasks.python import vision
    ssl.create_default_context(cafile=certifi.where())
    from gesture_recognition import load_settings, ensure_model
    gestures = root / 'app/gestures'
    load_settings(gestures / 'recognition_settings.json')
    json.loads((gestures / 'gesture_bindings.json').read_text(encoding='utf-8'))
    try:
        ensure_model(gestures / 'models/not-installed.task', allow_download=False)
    except FileNotFoundError:
        pass
    else:
        raise AssertionError('Missing model did not fail offline')
    if not args.skip_inference:
        image = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.zeros((256, 256, 3), dtype=np.uint8))
        with vision.GestureRecognizer.create_from_options(vision.GestureRecognizerOptions(
                base_options=python.BaseOptions(model_asset_buffer=(gestures / 'models/gesture_recognizer.task').read_bytes()))) as hand:
            hand.recognize(image)
        with vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
                base_options=python.BaseOptions(model_asset_buffer=(gestures / 'models/face_landmarker.task').read_bytes()))) as face:
            face.detect(image)
        ok, encoded = cv2.imencode('.jpg', np.zeros((64, 64, 3), dtype=np.uint8))
        assert ok and cv2.imdecode(encoded, cv2.IMREAD_COLOR).shape == (64, 64, 3)
    # No key and no upstream API request, even if the test runner has credentials.
    os.environ['COSMO_AI_PROVIDER'] = 'mock'
    from companion.http_api import TutorialHandler, ThreadingHTTPServer
    server = ThreadingHTTPServer(('127.0.0.1', 0), TutorialHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        base = f'http://127.0.0.1:{server.server_port}'
        with urllib.request.urlopen(base + '/health', timeout=3) as response:
            health = json.load(response)
        assert health['ok'] and health['service'] == 'ai-companion-tutorial'
        request = urllib.request.Request(base + '/events', b'{"event":"game_started"}',
                                         {'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=3) as response:
            event = json.load(response)
        assert event['tutorial']['advanced']
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)
    print(json.dumps({'ok': True, 'python': sys.version.split()[0],
                      'verified_files': len(manifest['files']), 'models_loaded': not args.skip_inference,
                      'opencv': cv2.__version__, 'mediapipe': mp.__version__, 'websockets': websockets.__version__}))


if __name__ == '__main__':
    main()
