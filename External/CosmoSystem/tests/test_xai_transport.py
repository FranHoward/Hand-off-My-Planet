import json
from io import BytesIO

import pytest

from companion.grok_client import XaiFunctionCallingClient, create_client
from companion.session import CompanionSession


def text_reply(text):
    return {"status": "completed", "output": [{"type": "message", "role": "assistant",
            "content": [{"type": "output_text", "text": text}]}]}


def client():
    session = CompanionSession()
    return session, XaiFunctionCallingClient(session.voice, session.functions, "test-key", "configured-test-model")


def test_real_transport_uses_configured_model_and_read_only_tools(monkeypatch):
    session, ai = client()
    before = session.tutorial.get_status().to_dict()
    requests = []
    def post(payload, timeout):
        requests.append(payload)
        assert payload["model"] == "configured-test-model" and timeout > 0
        if len(requests) == 1:
            assert payload["store"] is False and "messages" not in payload
            return {"status": "completed", "output": [
                {"type": "reasoning", "id": "reasoning-a", "encrypted_content": "opaque-reasoning-token", "summary": []},
                {"id": "fc-a", "type": "function_call", "call_id": "a", "name": "get_game_state", "arguments": "{}"},
                {"id": "fc-b", "type": "function_call", "call_id": "b", "name": "complete_stage", "arguments": "{}"},
            ]}
        assert any(m.get("encrypted_content") == "opaque-reasoning-token" for m in payload["input"])
        results = [m for m in payload["input"] if m.get("type") == "function_call_output"]
        assert [m["call_id"] for m in results] == ["a", "b"]
        assert json.loads(results[0]["output"])["player_health"] == 100
        assert "error" in json.loads(results[1]["output"])
        return text_reply("Try the assigned movement controls.")
    monkeypatch.setattr(ai, "_post", post)
    response = ai.reply_to_player("What do I do?")
    assert response["line"] == "Try the assigned movement controls."
    assert response["source"] == "grok"
    assert response["model"] == "configured-test-model"
    assert response["tools_used"] == ["get_game_state"]
    assert session.tutorial.get_status().to_dict() == before
    assert set(schema["name"] for schema in requests[0]["tools"]) == set(session.functions.names())


def test_provider_failure_keeps_local_guide_without_advancing(monkeypatch):
    session, ai = client()
    def fail(*args):
        raise TimeoutError()
    monkeypatch.setattr(ai, "_post", fail)
    response = ai.reply_to_player("I already moved")
    assert response["provider_error"] and response["line"]
    assert response["source"] == "fallback"
    assert session.tutorial.get_status().current_stage_id == "welcome"


def test_xai_provider_requires_server_configuration(monkeypatch):
    session = CompanionSession()
    monkeypatch.setenv("COSMO_AI_PROVIDER", "xai")
    monkeypatch.delenv("XAI_API_KEY", raising=False)
    monkeypatch.delenv("XAI_MODEL", raising=False)
    with pytest.raises(ValueError, match="XAI_API_KEY"):
        create_client(session.voice, session.functions)


def test_mock_source_never_claims_real_grok():
    session = CompanionSession()
    response = session.handle_player_speech("完成了")
    assert response["source"] == "mock"
    assert response["advanced"] is False
    assert session.tutorial.get_status().current_stage_id == "welcome"


def test_grok_session_source_and_offline_fallback_preserve_progress(monkeypatch):
    monkeypatch.setenv("COSMO_AI_PROVIDER", "grok")
    monkeypatch.setenv("XAI_API_KEY", "test-key")
    monkeypatch.setenv("XAI_MODEL", "configured-test-model")
    session = CompanionSession()
    monkeypatch.setattr(session.ai_client, "_post", lambda *args: text_reply("Wait for the game's confirmed event."))
    response = session.handle_player_speech("完成了")
    assert response["source"] == "grok" and response["provider"] == "xai"
    assert not response["advanced"]
    before = session.tutorial.get_status().to_dict()
    def offline(*args):
        raise OSError("Network disconnected")
    monkeypatch.setattr(session.ai_client, "_post", offline)
    response = session.handle_player_speech("完成了")
    assert response["source"] == "fallback" and response["provider"] == "local_fallback"
    assert response["line"] and response["provider_error"] and not response["advanced"]
    after = session.tutorial.get_status().to_dict()
    for field in ("current_stage_id", "completed_stage_ids", "expected_event", "is_complete"):
        assert after[field] == before[field]


def test_http_transport_posts_to_responses_and_reads_raw_output(monkeypatch):
    session, ai = client()
    def accepted(request, timeout):
        assert request.full_url == "https://api.x.ai/v1/responses"
        assert request.get_method() == "POST"
        assert request.get_header("Authorization") == "Bearer test-key"
        payload = json.loads(request.data)
        assert payload["model"] == "configured-test-model"
        assert payload["input"][-1] == {"role": "user", "content": "What next?"}
        assert "get_game_state" in {tool["name"] for tool in payload["tools"]}
        return BytesIO(json.dumps(text_reply("Wait for the game to start.")).encode())
    monkeypatch.setattr("companion.grok_client.urlopen", accepted)
    result = ai.reply_to_player("What next?")
    assert result["source"] == "grok" and result["line"] == "Wait for the game to start."
    assert session.tutorial.get_status().current_stage_id == "welcome"


@pytest.mark.parametrize("response", [
    {"status": "incomplete", "output": [{"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": "Partial"}]}]},
    {"status": "completed", "output": [{"type": "reasoning", "summary": [{"text": "Do not speak internal reasoning"}]}]},
    {"output": "invalid"},
    {"output": [None]},
    {"output": [{"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": 123}]}]},
])
def test_incomplete_or_malformed_responses_fall_back_without_claiming_grok(monkeypatch, response):
    session, ai = client()
    monkeypatch.setattr(ai, "_post", lambda *args: response)
    result = ai.reply_to_player("完成了")
    assert result["source"] == "fallback" and result["provider_error"]
    assert session.tutorial.get_status().current_stage_id == "welcome"
