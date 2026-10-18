"""Memory-only webcam preview for UE; never opens a second capture device."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock, Thread
import json
import time


class CameraPreview:
    def __init__(self, port=8767, *, fps=10, max_width=480):
        self.port, self.interval, self.max_width = port, 1 / fps, max_width
        self.lock = Lock()
        self.jpeg, self.updated_at, self.last_encode = b"", 0.0, 0.0
        self.server = None
        self.thread = None

    def start(self):
        preview = self
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def do_GET(self):
                with preview.lock:
                    image, age = preview.jpeg, time.monotonic() - preview.updated_at
                fresh = bool(image) and age < 2
                if self.path.split("?", 1)[0] == "/frame.jpg":
                    code, body, mime = (200, image, "image/jpeg") if fresh else (503, b"Camera not ready", "text/plain")
                elif self.path == "/health":
                    code, body, mime = 200, json.dumps({"ok": True, "camera_ready": fresh}).encode(), "application/json"
                else:
                    code, body, mime = 404, b"Not found", "text/plain"
                self.send_response(code)
                self.send_header("Content-Type", mime)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                try:
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass
        self.server = ThreadingHTTPServer(("127.0.0.1", self.port), Handler)
        self.server.daemon_threads = True
        self.port = self.server.server_port
        self.thread = Thread(target=self.server.serve_forever, name="camera-preview", daemon=True)
        self.thread.start()
        return self

    def publish(self, frame):
        now = time.monotonic()
        if now - self.last_encode < self.interval:
            return
        self.last_encode = now
        import cv2
        height, width = frame.shape[:2]
        if width > self.max_width:
            frame = cv2.resize(frame, (self.max_width, max(1, round(height * self.max_width / width))))
        ok, encoded = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
        if ok:
            with self.lock:
                self.jpeg, self.updated_at = encoded.tobytes(), now

    def stop(self):
        if self.server:
            self.server.shutdown()
            self.server.server_close()
        if self.thread:
            self.thread.join(timeout=2)
        with self.lock:
            self.jpeg = b""
