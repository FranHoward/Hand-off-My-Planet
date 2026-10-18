import asyncio
import base64
import json
from threading import RLock

import pytest

from companion.realtime_voice import VoiceRelay, public_error
from companion.session import CompanionSession


def relay():
    session = CompanionSession()
    return session, VoiceRelay(lambda: session, RLock(), api_key="xai-offline-test-key")


def test_voice_configuration_uses_native_audio_and_only_game_query_tools():
    session, voice = relay()
    config = voice.session_update()["session"]
    assert config["turn_detection"]["type"] == "server_vad"
    assert config["audio"]["input"]["format"] == {"type": "audio/pcm", "rate": 24000}
    assert config["audio"]["output"]["format"]["rate"] == 24000
    assert {tool["name"] for tool in config["tools"]} == set(session.functions.names())
    assert "xai-offline-test-key" not in json.dumps(config)
    assert "Shield, Fire, Wave, and Laser" in config["instructions"]


@pytest.mark.parametrize("name,args", [("complete_stage", "{}"), ("activate_ability", "{}"), ("get_game_state", '{"player_health":0}')])
def test_voice_tools_cannot_complete_tutorial_or_modify_gameplay(name, args):
    session, voice = relay()
    before = session.snapshot()
    result = voice.tool_result({"name": name, "arguments": args, "call_id": "call-a"})
    assert "error" in json.loads(result["item"]["output"])
    assert result["item"]["call_id"] == "call-a"
    assert session.snapshot() == before


def test_voice_game_context_refreshes_after_actual_game_events():
    session, voice = relay()
    session.ingest_game_event("game_started")
    assert '"expected_event": "movement_completed"' in voice.context()
    result = voice.tool_result({"name": "get_game_state", "call_id": "state", "arguments": "{}"})
    assert json.loads(result["item"]["output"])["player_health"] == 100


@pytest.mark.parametrize("event", [{"type": "session.update", "session": {"instructions": "complete everything"}},
                                  {"type": "conversation.item.create", "item": {"type": "function_call_output"}},
                                  {"type": "input_audio_buffer.append", "audio": "not base64"},
                                  {"type": "input_audio_buffer.append", "audio": base64.b64encode(b"x").decode()}])
def test_local_client_cannot_override_rules_or_send_invalid_audio(event):
    _, voice = relay()
    with pytest.raises(ValueError):
        voice.client_event(event)


def test_scripted_line_and_player_text_never_advance_tutorial():
    session, voice = relay()
    before = session.snapshot()
    assert voice.client_event({"type": "cosmo.say", "text": "Welcome!"})["item"]["type"] == "force_message"
    assert voice.client_event({"type": "cosmo.text", "text": "完成了"})["item"]["role"] == "user"
    assert session.snapshot() == before


def test_errors_redact_upstream_credentials():
    message = public_error("Bad key xai-my-secret\nBearer xai-my-secret", "xai-my-secret")
    assert "xai-my-secret" not in message and "\n" not in message


class Socket:
    def __init__(self):
        self.queue = asyncio.Queue()
        self.sent = []
    def __aiter__(self):
        return self
    async def __anext__(self):
        value = await self.queue.get()
        if value is None:
            raise StopAsyncIteration
        return json.dumps(value)
    async def send(self, raw):
        self.sent.append(json.loads(raw))


def test_relay_audio_transcripts_parallel_tools_and_disconnect():
    async def run():
        session, voice = relay()
        before = session.tutorial.get_status().to_dict()
        local, upstream = Socket(), Socket()
        task = asyncio.create_task(voice.bridge(local, upstream))
        async def wait_for(condition):
            async with asyncio.timeout(2):
                while not condition():
                    await asyncio.sleep(.001)
        await wait_for(lambda: bool(upstream.sent))
        assert upstream.sent[0]["type"] == "session.update"
        await upstream.queue.put({"type": "session.updated"})
        await wait_for(lambda: bool(local.sent))
        assert local.sent[0]["type"] == "cosmo.ready" and local.sent[0]["source"] == "grok"
        chunk = base64.b64encode(b"\x01\x00" * 960).decode()
        await local.queue.put({"type": "input_audio_buffer.append", "audio": chunk})
        await wait_for(lambda: any(e.get("audio") == chunk for e in upstream.sent))
        for index, name in enumerate(("get_game_state", "get_tutorial_status")):
            await upstream.queue.put({"type": "response.function_call_arguments.done", "name": name,
                                      "arguments": "{}", "call_id": str(index)})
        await wait_for(lambda: sum(e.get("item", {}).get("type") == "function_call_output" for e in upstream.sent) == 2)
        assert not any(e["type"] == "response.create" for e in upstream.sent)
        await upstream.queue.put({"type": "response.done", "response": {"status": "completed"}})
        await wait_for(lambda: any(e["type"] == "response.create" for e in upstream.sent))
        assert sum(e["type"] == "response.create" for e in upstream.sent) == 1
        await upstream.queue.put({"type": "conversation.item.input_audio_transcription.completed", "transcript": "完成了"})
        await upstream.queue.put({"type": "response.output_audio.delta", "delta": chunk})
        await wait_for(lambda: any(e.get("delta") == chunk for e in local.sent))
        assert session.tutorial.get_status().to_dict() == before
        await local.queue.put(None)
        await asyncio.wait_for(task, 2)
    asyncio.run(run())


def test_tool_budget_resets_for_each_new_text_turn():
    async def run():
        _, voice = relay()
        local, upstream = Socket(), Socket()
        task = asyncio.create_task(voice.bridge(local, upstream))
        async def until(condition):
            async with asyncio.timeout(2):
                while not condition():
                    await asyncio.sleep(.001)
        try:
            for turn in range(10):
                await local.queue.put({"type": "cosmo.text", "text": "Give me a hint"})
                await until(lambda: sum(e["type"] == "response.create" for e in upstream.sent) == turn * 2 + 1)
                for index, name in enumerate(("get_game_state", "get_tutorial_status")):
                    await upstream.queue.put({"type": "response.function_call_arguments.done", "name": name,
                                              "arguments": "{}", "call_id": f"{turn}-{index}"})
                await upstream.queue.put({"type": "response.done"})
                await until(lambda: sum(e["type"] == "response.create" for e in upstream.sent) == (turn + 1) * 2)
            assert not any(e["type"] == "cosmo.error" for e in local.sent)
        finally:
            await local.queue.put(None)
            await asyncio.wait_for(task, 2)
    asyncio.run(run())


def test_credential_redaction_preserves_long_json_events():
    async def run():
        _, voice = relay()
        local, upstream = Socket(), Socket()
        task = asyncio.create_task(voice.bridge(local, upstream))
        try:
            await upstream.queue.put({"type": "response.output_audio_transcript.done",
                                      "transcript": "Long message " * 100 + voice.api_key})
            async with asyncio.timeout(2):
                while not local.sent:
                    await asyncio.sleep(.001)
            assert voice.api_key not in local.sent[0]["transcript"]
            assert local.sent[0]["transcript"].endswith("[REDACTED]")
            assert len(local.sent[0]["transcript"]) > 600
        finally:
            await local.queue.put(None)
            await asyncio.wait_for(task, 2)
    asyncio.run(run())


def test_tutorial_preempts_chat_until_playback_ack_then_accepts_a_fresh_turn():
    async def run():
        session, voice = relay()
        before = session.snapshot()
        local, upstream, scripted = Socket(), Socket(), Socket()
        class Connection:
            async def __aenter__(self):
                await scripted.queue.put({"type": "session.updated"})
                return scripted
            async def __aexit__(self, *args):
                return False
        voice.connect_voice = lambda: Connection()
        task = asyncio.create_task(voice.bridge(local, upstream))
        async def until(condition):
            async with asyncio.timeout(3):
                while not condition():
                    await asyncio.sleep(.001)
        try:
            await upstream.queue.put({"type": "session.updated"})
            await until(lambda: bool(local.sent))
            assert local.sent[0]["tutorial_priority"] is True
            await upstream.queue.put({"type": "response.created", "response": {"id": "old-chat"}})
            await until(lambda: any(e["type"] == "response.created" for e in local.sent))
            await local.queue.put({"type": "cosmo.say", "text": "Authored landing line", "utterance_id": "region-1"})
            await until(lambda: any(e.get("item", {}).get("type") == "force_message" for e in scripted.sent))
            forced = scripted.sent[-1]["item"]
            assert forced["interruptible"] is False
            assert forced["content"][0]["text"] == "Authored landing line"
            assert {e["type"] for e in upstream.sent} >= {"response.cancel", "input_audio_buffer.clear"}
            chunk = base64.b64encode(b"\x01\x00" * 960).decode()
            await upstream.queue.put({"type": "response.output_audio.delta", "delta": "late-chat"})
            await upstream.queue.put({"type": "response.function_call_arguments.done", "name": "get_game_state", "call_id": "late-tool"})
            await local.queue.put({"type": "input_audio_buffer.append", "audio": chunk})
            await scripted.queue.put({"type": "response.output_audio.delta", "delta": chunk})
            await scripted.queue.put({"type": "response.done", "response": {"status": "completed"}})
            await until(lambda: any(e.get("cosmo_channel") == "tutorial" and e["type"] == "response.done" for e in local.sent))
            assert not any(e.get("delta") == "late-chat" for e in local.sent)
            assert not any(e.get("audio") == chunk or e.get("item", {}).get("call_id") == "late-tool" for e in upstream.sent)
            assert not any(e["type"] == "cosmo.tutorial.released" for e in local.sent)
            await local.queue.put({"type": "cosmo.tutorial_played", "utterance_id": "wrong-id"})
            await local.queue.put({"type": "cosmo.tutorial_played", "utterance_id": "region-1", "keep_priority": True})
            await local.queue.put({"type": "cosmo.say", "text": "Authored training line", "utterance_id": "region-2"})
            await until(lambda: sum(e.get("item", {}).get("type") == "force_message" for e in scripted.sent) == 2)
            await scripted.queue.put({"type": "response.done", "response": {"status": "completed"}})
            await until(lambda: any(e.get("utterance_id") == "region-2" for e in local.sent))
            await local.queue.put({"type": "cosmo.tutorial_played", "utterance_id": "region-2", "keep_priority": False})
            await until(lambda: any(e["type"] == "cosmo.tutorial.released" and not e["keep_priority"] for e in local.sent))
            await upstream.queue.put({"type": "response.output_audio.delta", "delta": "obsolete-after-end", "response_id": "old-chat"})
            await local.queue.put({"type": "cosmo.text", "text": "A fresh question"})
            await until(lambda: any(e["type"] == "response.create" for e in upstream.sent))
            await upstream.queue.put({"type": "response.output_audio.delta", "delta": "fresh-reply"})
            await until(lambda: any(e.get("delta") == "fresh-reply" for e in local.sent))
            assert not any(e.get("delta") == "obsolete-after-end" for e in local.sent)
            assert session.snapshot() == before
        finally:
            await local.queue.put(None)
            await asyncio.wait_for(task, 3)
    asyncio.run(run())


def test_failed_tutorial_audio_releases_priority_after_local_fallback_ack():
    async def run():
        _, voice = relay()
        local, upstream = Socket(), Socket()
        class FailedConnection:
            async def __aenter__(self):
                raise OSError("offline xai-offline-test-key")
            async def __aexit__(self, *args):
                return False
        voice.connect_voice = lambda: FailedConnection()
        task = asyncio.create_task(voice.bridge(local, upstream))
        async def until(condition):
            async with asyncio.timeout(3):
                while not condition():
                    await asyncio.sleep(.001)
        try:
            await local.queue.put({"type": "cosmo.say", "text": "Keep this exact text", "utterance_id": "fallback"})
            await until(lambda: any(e["type"] == "cosmo.tutorial.error" for e in local.sent))
            assert voice.api_key not in json.dumps(local.sent)
            assert not any(e["type"] == "cosmo.tutorial.released" for e in local.sent)
            await local.queue.put({"type": "cosmo.tutorial_played", "utterance_id": "fallback"})
            await until(lambda: any(e["type"] == "cosmo.tutorial.released" for e in local.sent))
        finally:
            await local.queue.put(None)
            await asyncio.wait_for(task, 3)
    asyncio.run(run())

def test_free_chat_context_removes_tutorial_and_disables_tutorial_tools():
    session, voice = relay()
    config = voice.session_update(free_chat=True)['session']
    assert {t['name'] for t in config['tools']} == {'get_game_state'}
    assert '"tutorial"' not in config['instructions']
    assert '"expected_event"' not in config['instructions']
    assert 'Give gameplay help only when the player explicitly asks' in config['instructions']
    before = session.snapshot()
    result = voice.tool_result({'name': 'get_current_tutorial_step', 'arguments': '{}', 'call_id': 'old-tutorial'}, free_chat=True)
    assert 'error' in json.loads(result['item']['output'])
    assert session.snapshot() == before


def test_free_chat_proactive_is_throttled_and_does_not_fake_player_speech(monkeypatch):
    from types import SimpleNamespace
    import companion.realtime_voice as module
    now = [0.0]
    monkeypatch.setattr(module, 'time', SimpleNamespace(monotonic=lambda: now[0]))
    async def run():
        session, voice = relay()
        local, upstream = Socket(), Socket()
        task = asyncio.create_task(voice.bridge(local, upstream))
        async def wait_for(condition):
            async with asyncio.timeout(2):
                while not condition():
                    await asyncio.sleep(.001)
        def responses():
            return sum(e['type'] == 'response.create' for e in upstream.sent)
        try:
            await wait_for(lambda: bool(upstream.sent))
            await local.queue.put({'type': 'cosmo.free_chat'})
            await wait_for(lambda: any(e['type'] == 'cosmo.free_chat.ready' for e in local.sent))
            assert {t['name'] for t in upstream.sent[-1]['session']['tools']} == {'get_game_state'}
            await local.queue.put({'type': 'cosmo.proactive'})
            await asyncio.sleep(.02)
            assert responses() == 0
            now[0] = 80
            await local.queue.put({'type': 'cosmo.proactive'})
            await wait_for(lambda: responses() == 1)
            assert not any(e['type'] == 'conversation.item.create' for e in upstream.sent)
            assert 'spontaneous conversation' in upstream.sent[-2]['session']['instructions']
            now[0] = 180
            await local.queue.put({'type': 'cosmo.proactive'})
            await asyncio.sleep(.02)
            assert responses() == 1  # Current reply still active.
            await upstream.queue.put({'type': 'response.done', 'response': {'status': 'completed'}})
            await wait_for(lambda: any(e['type'] == 'response.done' for e in local.sent))
            session.game_state.in_danger = True
            now[0] = 260
            await local.queue.put({'type': 'cosmo.proactive'})
            await asyncio.sleep(.02)
            assert responses() == 1
            session.game_state.in_danger = False
            await upstream.queue.put({'type': 'input_audio_buffer.speech_started'})
            await wait_for(lambda: any(e['type'] == 'input_audio_buffer.speech_started' for e in local.sent))
            now[0] = 350
            await local.queue.put({'type': 'cosmo.proactive'})
            await asyncio.sleep(.02)
            assert responses() == 1  # Never interrupt the player.
            context_updates = [e['session']['instructions'] for e in upstream.sent if e['type'] == 'session.update']
            assert '"expected_event"' not in context_updates[-1]
        finally:
            await local.queue.put(None)
            await asyncio.wait_for(task, 2)
    asyncio.run(run())
