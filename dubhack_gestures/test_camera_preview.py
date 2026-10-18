import json
from urllib.error import HTTPError
from urllib.request import urlopen

import cv2
import numpy as np
import pytest

from camera_preview import CameraPreview


def test_preview_serves_current_camera_frame_without_opening_another_camera(monkeypatch):
    monkeypatch.setattr(cv2, "VideoCapture", lambda *args: pytest.fail("Preview must reuse the recognition frame"))
    preview = CameraPreview(0).start()
    url = f"http://127.0.0.1:{preview.port}"
    try:
        with pytest.raises(HTTPError) as error:
            urlopen(url + "/frame.jpg", timeout=2)
        assert error.value.code == 503
        preview.publish(np.full((540, 960, 3), (20, 100, 220), dtype=np.uint8))
        with urlopen(url + "/frame.jpg", timeout=2) as response:
            assert response.headers["Cache-Control"] == "no-store"
            image = cv2.imdecode(np.frombuffer(response.read(), dtype=np.uint8), cv2.IMREAD_COLOR)
        assert image.shape[:2] == (270, 480)
        with urlopen(url + "/health", timeout=2) as response:
            assert json.load(response)["camera_ready"] is True
        preview.updated_at = 0
        with pytest.raises(HTTPError) as error:
            urlopen(url + "/frame.jpg", timeout=2)
        assert error.value.code == 503
    finally:
        preview.stop()
    assert not preview.thread.is_alive() and not preview.jpeg
