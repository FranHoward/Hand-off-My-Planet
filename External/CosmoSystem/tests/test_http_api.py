from __future__ import annotations

import json
import re
from http.client import HTTPConnection
from threading import Thread

from companion import http_api


def test_http_event_advances_when_expected(monkeypatch) -> None:
    http_api._SESSION = None
    session = http_api.reset_session()
    assert session.tutorial.get_status().current_stage_id == "welcome"

    server, thread = _start_server()
    try:
        conn = HTTPConnection("127.0.0.1", server.server_port, timeout=2)
        conn.request("POST", "/events", json.dumps({"event": "game_started"}), {"Content-Type": "application/json"})
        body = json.loads(conn.getresponse().read())
        assert body["tutorial"]["advanced"] is True
        conn.request("GET", "/tutorial/status")
        status = json.loads(conn.getresponse().read())
        assert status["current_stage_id"] == "movement"
        conn.request(
            "POST",
            "/player/utterance",
            json.dumps({"text": "I already moved"}),
            {"Content-Type": "application/json"},
        )
        speech = json.loads(conn.getresponse().read())
        assert speech["advanced"] is False
        assert speech["source"] == "mock"
        conn.request("GET", "/tutorial/status")
        status = json.loads(conn.getresponse().read())
        assert status["current_stage_id"] == "movement"
        conn.request("GET", "/")
        home = conn.getresponse()
        html = home.read()
        assert home.status == 200
        assert b"Hold to talk" in html
        conn.close()
    finally:
        server.shutdown()
        thread.join(timeout=2)
        server.server_close()


def test_http_final_abilities_full_progression_aliases_reset_and_voice_ui():
    http_api.reset_session()
    server, thread = _start_server()
    conn = HTTPConnection("127.0.0.1", server.server_port, timeout=2)

    def request(method, path, payload=None):
        conn.request(method, path, None if payload is None else json.dumps(payload), {"Content-Type": "application/json"})
        response = conn.getresponse()
        body = response.read()
        assert response.status == 200
        return json.loads(body)

    progression = [
        "game_started", "movement_completed", "shield_activated", "fire_activated",
        "wave_activated", "laser_activated", "enemy_scanned", "boss_encountered", "tutorial_completed",
    ]
    try:
        assert request("GET", "/health")["ok"]
        events = request("GET", "/events")["events"]
        assert events[:9] == progression
        assert set(events[9:]) == {"player_damaged", "player_in_danger", "danger_cleared"}
        for current in progression:
            before = request("GET", "/snapshot")
            assert before["tutorial"]["expected_event"] == current
            assert set(before["game_state"]["abilities"]) == {"shield", "fire", "wave", "laser"}
            for old_event in ("ice_spell_cast", "ice_activated", "freeze_cast", "fire_spell_cast"):
                rejected = request("POST", "/events", {"event": old_event})
                assert not rejected["tutorial"]["accepted"]
                assert not rejected["tutorial"]["advanced"]
                assert request("GET", "/snapshot") == before
            speech = request("POST", "/voice", {"utterance": "I already activated all four abilities"})
            assert speech["advanced"] is False
            assert request("GET", "/tutorial/status")["expected_event"] == current
            result = request("POST", "/game/event", {"event_name": current})
            assert result["tutorial"]["advanced"] is True
            assert not request("POST", "/events", {"event": current})["tutorial"]["advanced"]
        assert request("GET", "/tutorial/status")["is_complete"]
        reset = request("POST", "/reset", {})
        assert reset["tutorial"]["current_stage_id"] == "welcome"
        assert not any(reset["game_state"]["ability_confirmations"].values())
        assert request("GET", "/tutorial/step")["expected_event"] == "game_started"
        assert request("GET", "/tutorial/hint")["hint"]
        assert request("GET", "/companion")["state"]
        for url in ("/", "/index.html", "/static/index.html"):
            conn.request("GET", url)
            response = conn.getresponse()
            html = response.read().decode()
            assert response.status == 200
            for control in ("voiceSettingsTitle", "voiceSelect", "testVoice", "stopSpeaking", "speechRate", "speechPitch", "speechVolume", "toggleSpeak", "holdTalk"):
                assert f'id="{control}"' in html
            frontend_events = json.loads(re.search(r"const EVENTS = (\[[\s\S]*?\]);", html).group(1))
            assert frontend_events[:9] == progression
            assert "createBrowserTTS" in html
            assert "voiceschanged" in html
            assert "localStorage" in html
    finally:
        conn.close()
        server.shutdown()
        thread.join(timeout=2)
        server.server_close()


def _start_server():
    from http.server import ThreadingHTTPServer

    server = ThreadingHTTPServer(("127.0.0.1", 0), http_api.TutorialHandler)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread
