import json
from http.client import HTTPConnection

import pytest

from companion.game_state import GameState
from companion import http_api
from test_http_api import _start_server


def test_live_ue_state_is_transactional_and_cannot_complete_tutorial():
    state = GameState()
    result = state.update_live_state({"player_health": 37, "shield_up": False,
                                     "current_target": None, "boss_health": None,
                                     "abilities": {"fire": False}})
    assert result["player_health"] == 37
    assert result["current_target"] is None
    assert result["abilities"]["fire"] is False
    assert not any(result["ability_confirmations"].values())
    before = state.to_dict()
    for invalid in ({"started": True}, {"ability_confirmations": {"fire": True}},
                    {"player_health": 10, "shield_up": "yes"}, {"player_health": float("nan")},
                    {"abilities": {"fire": 1}}, {"player_health": -1}):
        with pytest.raises(ValueError):
            state.update_live_state(invalid)
        assert state.to_dict() == before


def test_http_telemetry_changes_voice_facts_without_advancing():
    http_api.reset_session()
    server, thread = _start_server()
    conn = HTTPConnection("127.0.0.1", server.server_port, timeout=2)
    def request(path, payload):
        conn.request("POST", path, json.dumps(payload), {"Content-Type": "application/json"})
        response = conn.getresponse()
        return response.status, json.loads(response.read())
    try:
        status, body = request("/game/state", {"player_health": 37, "current_target": "UE Guardian",
                                              "boss_health": 71, "abilities": {"fire": False}})
        assert status == 200
        assert body["game_state"]["player_health"] == 37
        status, body = request("/player/utterance", {"text": "Tell me about the boss"})
        assert status == 200 and not body["advanced"]
        assert "UE Guardian" in body["spoken_line"] and "37" in body["spoken_line"]
        assert body["status"]["expected_event"] == "game_started"
        status, _ = request("/game/state", {"tutorial_finished": True})
        assert status == 400
        status, _ = request("/game/state", [])
        assert status == 400
    finally:
        conn.close()
        server.shutdown()
        thread.join(timeout=2)
        server.server_close()
