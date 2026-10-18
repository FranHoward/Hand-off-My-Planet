"""Verbatim tutorial audio on a separate stream, isolated from cancelled chat replies."""
import asyncio
import json


class TutorialVoiceLane:
    def __init__(self, connect, configuration, send_client, sanitize):
        self.connect, self.configuration = connect, configuration
        self.send_client, self.sanitize = send_client, sanitize
        self.active = False
        self.allow_chat = True
        self.current_id = ""
        self.keep_priority = False
        self.played = asyncio.Event()
        self.requests = asyncio.Queue(maxsize=1)

    def submit(self, event, force_message):
        identifier = event.get("utterance_id", "")
        if not isinstance(identifier, str) or not identifier or len(identifier) > 80:
            raise ValueError("Tutorial speech requires an utterance ID")
        if self.current_id:
            raise ValueError("Wait until the current tutorial line finishes playing")
        self.active, self.allow_chat = True, False
        self.current_id = identifier
        self.keep_priority = False
        self.played.clear()
        self.requests.put_nowait((identifier, force_message))

    def acknowledge(self, event):
        if event.get("utterance_id") != self.current_id:
            return
        self.keep_priority = event.get("keep_priority") is True
        self.played.set()

    async def run(self):
        connection = socket = None
        async def close():
            nonlocal connection, socket
            if connection:
                await connection.__aexit__(None, None, None)
            connection = socket = None
        try:
            while True:
                identifier, message = await self.requests.get()
                try:
                    if socket is None:
                        candidate = self.connect()
                        socket = await candidate.__aenter__()
                        connection = candidate
                        await socket.send(json.dumps(self.configuration()))
                        async def configured():
                            async for raw in socket:
                                event = json.loads(raw)
                                if event.get("type") == "error":
                                    raise RuntimeError(str(event.get("error")))
                                if event.get("type") == "session.updated":
                                    return
                            raise RuntimeError("Tutorial voice disconnected")
                        await asyncio.wait_for(configured(), 20)
                    await socket.send(json.dumps(message, ensure_ascii=False))
                    async def generate():
                        async for raw in socket:
                            event = json.loads(raw)
                            if event.get("type") == "error":
                                raise RuntimeError(str(event.get("error")))
                            event.update(cosmo_channel="tutorial", utterance_id=identifier)
                            await self.send_client(event)
                            if event.get("type") == "response.done":
                                if event.get("response", {}).get("status") != "completed":
                                    raise RuntimeError("Tutorial voice did not complete")
                                return
                        raise RuntimeError("Tutorial voice disconnected")
                    generation = asyncio.create_task(generate())
                    early_playback = asyncio.create_task(self.played.wait())
                    try:
                        done, _ = await asyncio.wait([generation, early_playback], timeout=45,
                                                     return_when=asyncio.FIRST_COMPLETED)
                        if not done:
                            raise asyncio.TimeoutError("Tutorial audio timed out")
                        if generation in done:
                            generation.result()
                        else:
                            # Local fallback has already played the line; cancel
                            # a stalled remote stream before releasing chat.
                            generation.cancel()
                            await asyncio.gather(generation, return_exceptions=True)
                            await close()
                    finally:
                        generation.cancel()
                        early_playback.cancel()
                        await asyncio.gather(generation, early_playback, return_exceptions=True)
                except Exception as exc:
                    await self.send_client({"type": "cosmo.tutorial.error", "utterance_id": identifier,
                                            "message": self.sanitize(str(exc))})
                    await close()
                # Generation done isn't playback done. UE acknowledges after its
                # procedural audio queue (or local fallback speech) is drained.
                try:
                    await asyncio.wait_for(self.played.wait(), 120)
                except asyncio.TimeoutError:
                    self.keep_priority = False
                self.current_id = ""
                self.active = self.keep_priority
                await self.send_client({"type": "cosmo.tutorial.released", "utterance_id": identifier,
                                        "keep_priority": self.keep_priority})
        finally:
            await close()
