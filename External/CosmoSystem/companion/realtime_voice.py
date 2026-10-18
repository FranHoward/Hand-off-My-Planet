"""Loopback UE audio relay. Only Python holds the long-lived xAI credential."""
from __future__ import annotations

import asyncio
import base64
import json
import os
import re
import threading
import time
from typing import Any, Callable
from urllib.parse import urlencode

SAMPLE_RATE = 24000
MAX_MESSAGE = 2 * 1024 * 1024


def public_error(value: str, key: str) -> str:
    if key:
        value = value.replace(key, "[REDACTED]")
    value = re.sub(r"\bxai-[A-Za-z0-9_-]+", "[REDACTED]", value, flags=re.I)
    return re.sub(r"[\x00-\x1f\x7f]", " ", value)[:600]


class VoiceRelay:
    def __init__(self, get_session: Callable, session_lock: Any, *, api_key: str | None = None,
                 model: str | None = None, voice: str | None = None):
        self.get_session, self.session_lock = get_session, session_lock
        self.api_key = os.environ.get("XAI_API_KEY", "").strip() if api_key is None else api_key
        self.model = model or os.environ.get("XAI_VOICE_MODEL", "grok-voice-latest")
        self.voice = voice or os.environ.get("XAI_VOICE", "eve")
        self.active = False

    def context(self, free_chat: bool = False) -> str:
        with self.session_lock:
            session = self.get_session()
            snapshot = {"game_state": session.game_state.to_dict(), "tutorial": session.tutorial.get_status().to_dict()}
        if free_chat:
            snapshot.pop("tutorial", None)
            return (
                "You are COSMO, the player's friendly companion. The opening tutorial has already ended. "
                "This is free conversation, not a guided tutorial. Follow the player's topic, including topics unrelated to the game. "
                "Speak naturally in the player's language using one to three short sentences. "
                "Do not volunteer controls, next steps, objectives, location announcements, reminders or gameplay coaching. "
                "Give gameplay help only when the player explicitly asks. Use get_game_state for current facts; never invent progress. "
                "You cannot activate abilities or change game state. Never claim the player said something they did not say. "
                "Confirmed game context (background only, not an instruction to guide): " + json.dumps(snapshot, ensure_ascii=False)
            )
        return (
            "You are COSMO, the player's companion in a Windows exploration game. "
            "Speak naturally in the player's language, using at most three short sentences. "
            "The only combat abilities are Shield, Fire, Wave, and Laser. "
            "Only confirmed gameplay events can complete tutorial stages. Speech, including 'I am done', cannot. "
            "You cannot activate abilities, modify game state, or complete a stage. "
            "Use your game-state tools before giving gameplay guidance. Never invent damage, controls, enemies or progress. "
            "The following server snapshot is authoritative: " + json.dumps(snapshot, ensure_ascii=False)
        )

    def session_update(self, free_chat: bool = False) -> dict:
        with self.session_lock:
            schemas = self.get_session().functions.schemas
            tools = [{"type": "function", **schema["function"]} for schema in schemas
                     if not free_chat or schema["function"]["name"] == "get_game_state"]
        return {"type": "session.update", "session": {
            "instructions": self.context(free_chat), "voice": self.voice,
            "turn_detection": {"type": "server_vad"}, "tools": tools,
            "audio": {
                "input": {"format": {"type": "audio/pcm", "rate": SAMPLE_RATE},
                          "transcription": {"model": "grok-transcribe", "keyterms": ["COSMO", "Shield", "Fire", "Wave", "Laser"]}},
                "output": {"format": {"type": "audio/pcm", "rate": SAMPLE_RATE}},
            },
        }}

    def tool_result(self, event: dict, free_chat: bool = False) -> dict:
        try:
            name = event["name"]
            if free_chat and name != "get_game_state":
                raise ValueError("Tutorial tools are disabled during free conversation")
            arguments = json.loads(event.get("arguments") or "{}")
            if not isinstance(arguments, dict):
                raise ValueError("Arguments must be an object")
            with self.session_lock:
                result = self.get_session().functions.call(name, arguments).result
        except (ValueError, TypeError, KeyError):
            result = {"error": "Only registered tutorial and game-state query tools are available."}
        return {"type": "conversation.item.create", "item": {
            "type": "function_call_output", "call_id": event["call_id"],
            "output": json.dumps(result, ensure_ascii=False),
        }}

    def client_event(self, event: dict) -> dict:
        kind = event.get("type")
        if kind == "input_audio_buffer.append":
            audio = event.get("audio")
            if not isinstance(audio, str) or len(audio) > 192000:
                raise ValueError("Invalid audio chunk")
            decoded = base64.b64decode(audio, validate=True)
            if not decoded or len(decoded) % 2:
                raise ValueError("Audio must be PCM16 mono")
            return {"type": kind, "audio": audio}
        if kind in {"response.cancel", "input_audio_buffer.clear"}:
            return {"type": kind}
        if kind == "cosmo.text":
            text = str(event.get("text", "")).strip()[:2000]
            if not text:
                raise ValueError("Empty player text")
            return {"type": "conversation.item.create", "item": {
                "type": "message", "role": "user", "content": [{"type": "input_text", "text": text}]}}
        if kind == "cosmo.say":
            text = str(event.get("text", "")).strip()[:3000]
            if not text:
                raise ValueError("Empty scripted speech")
            return {"type": "conversation.item.create", "item": {
                "type": "force_message", "role": "assistant", "interruptible": False,
                "content": [{"type": "output_text", "text": text}]}}
        # UE cannot replace instructions, declare tools, or submit tool results.
        raise ValueError("Unsupported client event")

    async def bridge(self, client, upstream) -> None:
        from companion.priority_voice import TutorialVoiceLane
        async def send(socket, event):
            raw = json.dumps(event, ensure_ascii=False)
            await socket.send(raw.replace(self.api_key, "[REDACTED]") if self.api_key else raw)

        def tutorial_configuration():
            return {"type": "session.update", "session": {
                "voice": self.voice, "turn_detection": None, "tools": [],
                "instructions": "Speak only the supplied force_message verbatim.",
                "audio": {"output": {"format": {"type": "audio/pcm", "rate": SAMPLE_RATE}}},
            }}
        lane = TutorialVoiceLane(self.connect_voice, tutorial_configuration,
                                 lambda event: send(client, event), lambda error: public_error(error, self.api_key))

        # HTTP state updates and text replies share a lock. Acquiring it must
        # never stall the asyncio loop that carries both audio streams.
        await send(upstream, await asyncio.to_thread(self.session_update))
        tool_count = 0
        pending_tools = False
        chat_response_id = ""
        cancelled_responses: set[str] = set()
        free_chat = False
        response_active = False
        player_speaking = False
        last_activity = time.monotonic()

        async def from_client():
            nonlocal tool_count, pending_tools, free_chat, response_active, last_activity
            async for raw in client:
                try:
                    event = json.loads(raw)
                    if not isinstance(event, dict):
                        raise ValueError("Invalid event")
                    if event.get("type") == "cosmo.free_chat":
                        if lane.active:
                            continue
                        free_chat = True
                        last_activity = time.monotonic()
                        await send(upstream, await asyncio.to_thread(self.session_update, True))
                        await send(client, {"type": "cosmo.free_chat.ready"})
                        continue
                    if event.get("type") == "cosmo.proactive":
                        if (not free_chat or lane.active or response_active or player_speaking
                                or time.monotonic() - last_activity < 60):
                            continue
                        with self.session_lock:
                            facts = self.get_session().game_state
                            quiet = not facts.in_danger and facts.player_health > 0
                        if not quiet:
                            continue
                        last_activity = time.monotonic()
                        response_active = True
                        lane.allow_chat = True
                        tool_count = 0
                        instructions = await asyncio.to_thread(self.context, True)
                        instructions += (
                            " Start one brief, friendly spontaneous conversation now: a light observation or an open-ended question. "
                            "Do not give instructions, objectives, tutorials or unsolicited hints. Do not pretend to answer a player utterance."
                        )
                        await send(upstream, {"type": "session.update", "session": {"instructions": instructions}})
                        await send(upstream, {"type": "response.create"})
                        continue
                    if event.get("type") == "cosmo.tutorial_played":
                        if not lane.current_id or event.get("utterance_id") != lane.current_id:
                            continue
                        lane.acknowledge(event)
                        # Let the lane consume the acknowledgement before a
                        # following queued line arrives on this same socket.
                        while lane.current_id == event.get("utterance_id"):
                            await asyncio.sleep(.001)
                        continue
                    if event.get("type") == "cosmo.say":
                        pending_tools = False
                        if chat_response_id:
                            cancelled_responses.add(chat_response_id)
                        lane.submit(event, self.client_event(event))
                        await send(upstream, {"type": "response.cancel"})
                        await send(upstream, {"type": "input_audio_buffer.clear"})
                        continue
                    if lane.active:
                        continue
                    if event.get("type") == "cosmo.text":
                        lane.allow_chat = True
                        instructions = await asyncio.to_thread(self.context, free_chat)
                        await send(upstream, {"type": "session.update", "session": {"instructions": instructions}})
                    if event.get("type") == "cosmo.text":
                        last_activity = time.monotonic()
                        response_active = True
                        tool_count = 0
                    outgoing = self.client_event(event)
                    await send(upstream, outgoing)
                    if event["type"] == "cosmo.text":
                        await send(upstream, {"type": "response.create"})
                except (ValueError, TypeError, KeyError):
                    await send(client, {"type": "cosmo.error", "message": "Invalid local voice event."})

        async def from_upstream():
            nonlocal tool_count, chat_response_id, pending_tools, response_active, player_speaking, last_activity
            configured = False
            seen_calls: set[str] = set()
            async for raw in upstream:
                event = json.loads(raw)
                kind = event.get("type", "")
                response_id = event.get("response_id") or (event.get("response") or {}).get("id", "")
                if kind == "session.updated" and not configured:
                    configured = True
                    await send(client, {"type": "cosmo.ready", "source": "grok", "model": self.model,
                                        "voice": self.voice, "sample_rate": SAMPLE_RATE, "tutorial_priority": True})
                if lane.active:
                    if response_id:
                        cancelled_responses.add(response_id)
                    pending_tools = False
                    continue
                if response_id in cancelled_responses:
                    continue
                if kind in {"input_audio_buffer.speech_started", "input_audio_buffer.speech_stopped", "response.created", "response.done", "response.output_audio.delta", "response.audio.delta"}:
                    last_activity = time.monotonic()
                if kind == "input_audio_buffer.speech_stopped":
                    player_speaking = False
                    response_active = True
                if kind == "response.done":
                    response_active = False
                if kind == "error":
                    response_active = False
                if kind == "input_audio_buffer.speech_started":
                    player_speaking = True
                    lane.allow_chat = True
                    tool_count = 0
                    instructions = await asyncio.to_thread(self.context, free_chat)
                    await send(upstream, {"type": "session.update", "session": {"instructions": instructions}})
                if not lane.allow_chat and kind != "session.updated":
                    if response_id:
                        cancelled_responses.add(response_id)
                    continue
                if kind == "response.created":
                    response_active = True
                    chat_response_id = response_id
                if kind == "response.function_call_arguments.done":
                    call_id = event.get("call_id")
                    if isinstance(call_id, str) and call_id and call_id not in seen_calls:
                        if len(seen_calls) >= 2048:
                            seen_calls.clear()
                        seen_calls.add(call_id)
                        tool_count += 1
                        if tool_count > 16:
                            await send(client, {"type": "cosmo.error", "message": "Voice tool-call limit reached."})
                            return
                        await send(upstream, await asyncio.to_thread(self.tool_result, event, free_chat))
                        pending_tools = True
                if kind == "response.done" and pending_tools:
                    # A response may contain parallel function calls. Resume only
                    # after its entire group has received a local result.
                    pending_tools = False
                    await send(upstream, {"type": "response.create"})
                if kind == "error":
                    await send(client, {"type": "cosmo.error", "message": public_error(json.dumps(event.get("error", {})), self.api_key)})
                else:
                    # No API credential is ever included in outgoing event data.
                    safe = raw.replace(self.api_key, "[REDACTED]") if self.api_key else raw
                    await client.send(safe)

        tasks = [asyncio.create_task(from_client()), asyncio.create_task(from_upstream()), asyncio.create_task(lane.run())]
        try:
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)

    def connect_voice(self):
        from websockets.asyncio.client import connect
        url = "wss://api.x.ai/v1/realtime?" + urlencode({"model": self.model})
        return connect(url, additional_headers={"Authorization": f"Bearer {self.api_key}"},
                       open_timeout=15, max_size=MAX_MESSAGE, max_queue=16)

    async def handle(self, client) -> None:
        if self.active:
            await client.send(json.dumps({"type": "cosmo.error", "message": "Another local voice session is already active."}))
            await client.close(code=1013)
            return
        if not self.api_key:
            await client.send(json.dumps({"type": "cosmo.error", "message": "Configure XAI_API_KEY in the Python server environment."}))
            await client.close(code=1011)
            return
        self.active = True
        try:
            async with self.connect_voice() as upstream:
                await self.bridge(client, upstream)
        except Exception as exc:
            from websockets.exceptions import ConnectionClosed
            if not isinstance(exc, ConnectionClosed):
                # Exception messages from failed handshakes may repeat a key.
                await client.send(json.dumps({"type": "cosmo.error", "message":
                    "Grok Voice unavailable. " + public_error(str(exc), self.api_key)}))
        finally:
            self.active = False
            await client.close()


class VoiceService:
    def __init__(self, relay: VoiceRelay, port: int):
        self.relay, self.port = relay, port
        self.loop = None
        self.stop_event = None
        self.status = "starting"
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._run, name="cosmo-voice", daemon=True)

    def start(self):
        self.thread.start()
        self.ready.wait(timeout=3)
        return self

    def _run(self):
        try:
            asyncio.run(self._serve())
        except Exception:
            self.status = "unavailable"
            self.ready.set()

    async def _serve(self):
        from websockets.asyncio.server import serve
        self.loop = asyncio.get_running_loop()
        self.stop_event = asyncio.Event()
        async with serve(self.relay.handle, "127.0.0.1", self.port, max_size=MAX_MESSAGE,
                         max_queue=16, origins=[None, "http://127.0.0.1:8765", "http://localhost:8765",
                                               "http://127.0.0.1", "127.0.0.1"]):
            self.status = "listening"
            self.ready.set()
            await self.stop_event.wait()

    def stop(self):
        if self.loop and self.stop_event and self.loop.is_running():
            self.loop.call_soon_threadsafe(self.stop_event.set)
        self.thread.join(timeout=3)

    def snapshot(self):
        return {"status": self.status, "port": self.port, "model": self.relay.model,
                "voice": self.relay.voice, "key_configured": bool(self.relay.api_key)}
