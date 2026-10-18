"""Synthetic loopback fixtures for native UE AV checks; no xAI or hardware access."""
import asyncio
import base64
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "dubhack_gestures"))
from camera_preview import CameraPreview
import cv2
import numpy as np
from websockets.asyncio.server import serve


async def main():
    camera = CameraPreview(18767).start()
    frame = np.zeros((540, 960, 3), dtype=np.uint8)
    frame[:] = (38, 25, 14)
    for x in range(0, 960, 80):
        cv2.line(frame, (x, 0), (x, 540), (65, 55, 40), 1)
    cv2.circle(frame, (480, 200), 95, (160, 205, 220), 3)
    cv2.ellipse(frame, (480, 485), (220, 180), 0, 180, 360, (160, 205, 220), 3)
    cv2.putText(frame, "CAMERA PREVIEW TEST", (120, 70), cv2.FONT_HERSHEY_SIMPLEX, 1, (80, 240, 200), 2)
    cv2.putText(frame, "LOCAL ONLY - NO RECORDING", (180, 515), cv2.FONT_HERSHEY_SIMPLEX, .7, (230, 230, 230), 2)
    stale = False
    async def publish():
        while True:
            if not stale:
                camera.publish(frame)
            await asyncio.sleep(.11)
    async def handle(client):
        nonlocal stale
        response_number=0
        async def send(kind, **fields):
            await client.send(json.dumps({"type": kind, **fields}))
        # This fixture deliberately mimics the source marker to exercise parsing;
        # fixture results are NEVER evidence of a real Grok API connection.
        await send("cosmo.ready", source="grok", sample_rate=24000, model="offline-fixture", tutorial_priority=True)
        async for raw in client:
            event = json.loads(raw)
            if event.get("text") == "offline stale camera":
                stale = True
                continue
            if event.get("type") == "cosmo.say":
                response_number+=1
                identifier=event["utterance_id"]
                fields={"cosmo_channel":"tutorial","utterance_id":identifier}
                await send("response.created",response={"id":f"fixture-{response_number}"},**fields)
                await send("input_audio_buffer.speech_started")  # Chat cannot interrupt the tutorial.
                await send("response.output_audio_transcript.done",transcript="Late chat must be ignored")
                await send("response.output_audio.delta",delta=base64.b64encode(struct.pack("<h", 300)*2400).decode(),**fields)
                await send("response.done",response={"id":f"fixture-{response_number}","status":"completed"},**fields)
            elif event.get("type") == "cosmo.text":
                response_number+=1
                response_id=f"fixture-{response_number}"
                await send("response.created", response={"id": response_id})
                await send("conversation.item.input_audio_transcription.completed", transcript="I am done")
                await send("response.output_audio.delta", response_id=response_id, delta=base64.b64encode(struct.pack("<h", 300) * 2400).decode())
                await send("response.output_audio_transcript.done", response_id=response_id, transcript="Offline voice fixture.")
                await send("response.done", response={"id": response_id, "status": "completed"})
    task = asyncio.create_task(publish())
    try:
        async with serve(handle, "127.0.0.1", 18766,
                         origins=[None, "http://127.0.0.1", "127.0.0.1"]):
            print("REALTIME_AV_FIXTURE_READY hardware=false xai=false", flush=True)
            await asyncio.Event().wait()
    finally:
        task.cancel()
        await asyncio.gather(task, return_exceptions=True)
        camera.stop()


if __name__ == "__main__":
    asyncio.run(main())
