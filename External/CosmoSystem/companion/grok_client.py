from __future__ import annotations

from typing import Any
from pathlib import Path
import json
import os
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from companion.voice import VoiceResponse

from companion.function_calling import FunctionRegistry
from companion.voice import GrokVoiceGuide


class GrokFunctionCallingClient:
    """Adapter for the Grok / xAI tool loop used in mock testing.

    Real Unreal builds should send the same `TUTORIAL_TOOLS` schemas to Grok
    and execute returned tool calls against `FunctionRegistry`. Tutorial
    mutation stays in Python (`TutorialManager`), never in the prompt.
    """

    def __init__(self, voice: GrokVoiceGuide, registry: FunctionRegistry):
        self.voice = voice
        self.registry = registry
        self.system_prompt = (
            Path(__file__).resolve().parents[1] / "data" / "companion_instructions.txt"
        ).read_text(encoding="utf-8")

    def tool_schemas(self) -> list[dict[str, Any]]:
        return list(self.registry.schemas)

    def execute_tool(self, name: str, arguments: dict[str, Any] | None = None) -> Any:
        return self.registry.call(name, arguments).result

    def reply_to_player(self, transcript: str) -> dict[str, Any]:
        return {**self.voice.handle_utterance(transcript).to_dict(), "source": "mock"}


class XaiFunctionCallingClient(GrokFunctionCallingClient):
    """Opt-in real Grok transport. The model only receives the four read-only game tools."""

    def __init__(self, voice: GrokVoiceGuide, registry: FunctionRegistry, api_key: str, model: str):
        super().__init__(voice, registry)
        if not api_key.strip() or not model.strip():
            raise ValueError("Set XAI_API_KEY and XAI_MODEL on the COSMO server before selecting xai")
        self._api_key = api_key
        self.model = model

    def _post(self, payload: dict[str, Any], timeout: float) -> dict[str, Any]:
        request = Request("https://api.x.ai/v1/responses", json.dumps(payload).encode("utf-8"),
                          {"Authorization": f"Bearer {self._api_key}", "Content-Type": "application/json",
                           "Accept": "application/json", "User-Agent": "VFX-COSMO/0.2"})
        with urlopen(request, timeout=timeout) as response:
            return json.loads(response.read(1024 * 1024))

    def reply_to_player(self, transcript: str) -> dict[str, Any]:
        self.voice.behavior.listening()
        messages: list[dict[str, Any]] = [
            {"role": "system", "content": self.system_prompt +
             "\nAnswer in the player's language, in at most three short sentences. "
             "Only the supplied gameplay facts are authoritative. You cannot activate abilities or complete stages."},
            {"role": "system", "content": "Current confirmed context: " + json.dumps({
                "game_state": self.registry.call("get_game_state").result,
                "tutorial": self.registry.call("get_tutorial_status").result,
                "step": self.registry.call("get_current_tutorial_step").result,
            }, ensure_ascii=False)},
            {"role": "user", "content": transcript},
        ]
        tools_used: list[str] = []
        deadline = time.monotonic() + 20
        try:
            for _ in range(4):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Grok response deadline exceeded")
                result = self._post({
                    "model": self.model, "input": list(messages),
                    "tools": [{"type": "function", **schema["function"]} for schema in self.tool_schemas()],
                    "tool_choice": "auto", "stream": False, "store": False,
                    "include": ["reasoning.encrypted_content"],
                }, remaining)
                if not isinstance(result, dict) or result.get("error") or result.get("status") not in {None, "completed"}:
                    raise ValueError("Grok did not complete a response")
                output = result.get("output")
                if not isinstance(output, list) or any(not isinstance(item, dict) for item in output):
                    raise ValueError("Grok returned invalid response output")
                calls = [item for item in output if item.get("type") == "function_call"]
                if not calls:
                    text_parts = []
                    for item in output:
                        if item.get("type") != "message" or item.get("role") != "assistant":
                            continue
                        content = item.get("content")
                        if not isinstance(content, list):
                            raise ValueError("Grok returned invalid message content")
                        for part in content:
                            if not isinstance(part, dict):
                                raise ValueError("Grok returned invalid content part")
                            if part.get("type") == "output_text":
                                if not isinstance(part.get("text"), str):
                                    raise ValueError("Grok returned invalid output text")
                                text_parts.append(part["text"])
                    line = "\n".join(text_parts).strip()
                    if not line:
                        raise ValueError("Grok returned an empty reply")
                    animation = self.voice.behavior.speaking(line, reason="grok_reply")
                    return {
                        **VoiceResponse(line, tools_used, "speaking", animation, False).to_dict(),
                        "source": "grok",
                        "model": self.model,
                    }
                # Replay typed output, including opaque encrypted reasoning, when
                # returning tool results. Never turn reasoning into subtitles.
                messages.extend(output)
                for call in calls:
                    name, call_id = call["name"], call["call_id"]
                    if not isinstance(name, str) or not isinstance(call_id, str) or not call_id:
                        raise ValueError("Grok returned an invalid function call")
                    try:
                        arguments = json.loads(call.get("arguments") or "{}")
                        if not isinstance(arguments, dict):
                            raise ValueError("Tool arguments must be an object")
                        value = self.execute_tool(name, arguments)
                        tools_used.append(name)
                    except (ValueError, TypeError, KeyError):
                        value = {"error": "Only registered read-only tutorial/game-state tools are available."}
                    messages.append({"type": "function_call_output", "call_id": call_id,
                                     "output": json.dumps(value, ensure_ascii=False)})
            raise ValueError("Grok tool-call limit exceeded")
        except (HTTPError, URLError, TimeoutError, ValueError, KeyError, IndexError, TypeError, OSError):
            # The local guide remains available, with an explicit provider error and no state mutation.
            fallback = super().reply_to_player(transcript)
            fallback["source"] = "fallback"
            fallback["provider_error"] = "Grok is unavailable; using the local tutorial guide."
            return fallback


def create_client(voice: GrokVoiceGuide, registry: FunctionRegistry) -> GrokFunctionCallingClient:
    provider = os.environ.get("COSMO_AI_PROVIDER", "local").strip().lower()
    if provider in {"local", "mock"}:
        return GrokFunctionCallingClient(voice, registry)
    if provider in {"xai", "grok"}:
        return XaiFunctionCallingClient(voice, registry, os.environ.get("XAI_API_KEY", ""),
                                       os.environ.get("XAI_MODEL", ""))
    raise ValueError("COSMO_AI_PROVIDER must be local/mock or xai/grok")
