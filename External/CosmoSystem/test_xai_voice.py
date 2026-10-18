"""Check the running loopback relay against real Grok Voice, without recording a microphone."""
import argparse
import asyncio
import base64
import json
import sys
from urllib.parse import urlparse


async def check(url):
    from websockets.asyncio.client import connect
    output = {"source": "none", "model": "", "voice": "", "line": "", "audio_bytes": 0}
    async with asyncio.timeout(45):
        async with connect(url, max_size=2 * 1024 * 1024) as socket:
            async for raw in socket:
                event = json.loads(raw)
                kind = event.get("type")
                if kind == "cosmo.error":
                    raise RuntimeError(event.get("message", "Voice relay error"))
                if kind == "cosmo.ready":
                    if event.get("source") != "grok":
                        raise RuntimeError("Relay did not report a Grok connection")
                    output.update(model=event.get("model", ""), voice=event.get("voice", ""))
                    await socket.send(json.dumps({"type": "cosmo.text", "text":
                        "Briefly introduce yourself as COSMO and name the four supported combat abilities."}))
                elif kind in {"response.output_audio.delta", "response.audio.delta"}:
                    output["audio_bytes"] += len(base64.b64decode(event.get("delta", ""), validate=True))
                elif kind in {"response.output_audio_transcript.delta", "response.audio_transcript.delta"}:
                    output["line"] += event.get("delta", "")
                elif kind in {"response.output_audio_transcript.done", "response.audio_transcript.done"}:
                    output["line"] = event.get("transcript", "") or output["line"]
                elif kind == "response.done" and output["audio_bytes"] and output["line"]:
                    if event.get("response", {}).get("status") != "completed":
                        raise RuntimeError("Voice response did not complete")
                    output["source"] = "grok"
                    return output
    raise RuntimeError("Connection ended before completed audio and transcript arrived")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="ws://127.0.0.1:8766")
    args = parser.parse_args()
    parsed = urlparse(args.url)
    if parsed.scheme != "ws" or parsed.hostname != "127.0.0.1":
        parser.error("Use the local Python relay at ws://127.0.0.1:<port>")
    try:
        result = asyncio.run(check(args.url))
        print(json.dumps(result, ensure_ascii=False))
    except Exception as exc:
        print("Grok Voice check failed: " + str(exc), file=sys.stderr)
        sys.exit(1)
