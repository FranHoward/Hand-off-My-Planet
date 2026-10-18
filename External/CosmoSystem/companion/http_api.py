from __future__ import annotations

import json
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse
from threading import RLock

from companion.game_state import KNOWN_EVENTS
from companion.session import CompanionSession

STATIC_DIR = Path(__file__).resolve().parents[1] / "static"
_SESSION: CompanionSession | None = None
_VOICE_SERVICE = None
SESSION_LOCK = RLock()


def get_session() -> CompanionSession:
    global _SESSION
    if _SESSION is None:
        _SESSION = CompanionSession()
        _SESSION.open()
    return _SESSION


def reset_session() -> CompanionSession:
    global _SESSION
    _SESSION = CompanionSession()
    _SESSION.open()
    return _SESSION


class TutorialHandler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: Any) -> None:  # noqa: A003
        return

    def _json(self, code: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _bytes(self, code: int, payload: bytes, content_type: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        if not raw:
            return {}
        return json.loads(raw.decode("utf-8"))

    def do_GET(self) -> None:  # noqa: N802
        with SESSION_LOCK:
            self._get()

    def _get(self) -> None:
        path = urlparse(self.path).path
        if path in {"/", "/index.html"}:
            page = STATIC_DIR / "index.html"
            self._bytes(200, page.read_bytes(), "text/html; charset=utf-8")
            return
        if path.startswith("/static/"):
            target = (STATIC_DIR / path[len("/static/") :]).resolve()
            if STATIC_DIR in target.parents or target == STATIC_DIR:
                if target.is_file():
                    mime = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
                    self._bytes(200, target.read_bytes(), mime)
                    return
            self._json(404, {"error": "not_found"})
            return

        session = get_session()
        if path == "/health":
            snapshot = session.snapshot()
            self._json(200, {"ok": True, "service": "ai-companion-tutorial",
                             "provider": snapshot["provider"], "model": snapshot["model"],
                             "realtime": _VOICE_SERVICE.snapshot() if _VOICE_SERVICE else {"status": "disabled"}})
            return
        if path == "/events":
            self._json(200, {"events": list(KNOWN_EVENTS)})
            return
        if path == "/tutorial/status":
            self._json(200, session.tutorial.get_status().to_dict())
            return
        if path == "/tutorial/step":
            self._json(200, session.functions.call("get_current_tutorial_step").result)
            return
        if path == "/tutorial/hint":
            self._json(200, session.functions.call("get_available_tutorial_hint").result)
            return
        if path == "/companion":
            self._json(200, session.behavior.snapshot())
            return
        if path == "/snapshot":
            self._json(200, session.snapshot())
            return
        self._json(404, {"error": "not_found"})

    def do_POST(self) -> None:  # noqa: N802
        with SESSION_LOCK:
            try:
                self._post()
            except (ValueError, UnicodeError, TypeError) as exc:
                self._json(400, {"error": str(exc)})

    def _post(self) -> None:
        path = urlparse(self.path).path
        session = get_session()
        payload = self._read_json()
        if not isinstance(payload, dict):
            raise ValueError("Request body must be a JSON object")
        if path == "/game/state":
            game = session.game_state.update_live_state(payload)
            self._json(200, {"game_state": game})
            return
        if path in {"/events", "/game/event"}:
            event = payload.get("event") or payload.get("event_name")
            if not event:
                self._json(400, {"error": "missing_event"})
                return
            self._json(200, session.ingest_game_event(str(event)))
            return
        if path in {"/player/utterance", "/voice"}:
            text = payload.get("text") or payload.get("utterance") or ""
            self._json(200, session.handle_player_speech(str(text)))
            return
        if path == "/reset":
            self._json(200, reset_session().snapshot())
            return
        self._json(404, {"error": "not_found"})


def serve(host: str = "127.0.0.1", port: int = 8765, voice_port: int = 0) -> None:
    global _VOICE_SERVICE
    server = ThreadingHTTPServer((host, port), TutorialHandler)
    if voice_port:
        from companion.realtime_voice import VoiceRelay, VoiceService
        _VOICE_SERVICE = VoiceService(VoiceRelay(get_session, SESSION_LOCK), voice_port).start()
    print(f"COSMO listening on http://{host}:{port}")
    print(f"Tester UI:                 http://{host}:{port}/")
    print(f"Health:                    http://{host}:{port}/health")
    print('POST /events {"event": "shield_activated"}')
    print('POST /player/utterance {"text": "What should I do?"}')
    if _VOICE_SERVICE:
        print(f"Realtime voice:            ws://127.0.0.1:{voice_port} ({_VOICE_SERVICE.status})")
    try:
        server.serve_forever()
    finally:
        server.server_close()
        if _VOICE_SERVICE:
            _VOICE_SERVICE.stop()
            _VOICE_SERVICE = None
