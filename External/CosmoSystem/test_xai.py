"""Explicit online acceptance check; pytest collection never calls the paid API."""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def safe_error_detail(error: HTTPError, key: str) -> str:
    """Show the server's reason without echoing credentials or terminal controls."""
    try:
        raw = error.read(8192).decode("utf-8", errors="replace")
    except (OSError, ValueError):
        return "Server error detail could not be read."
    # Some authentication errors repeat the supplied key. Redact before parsing
    # and again after parsing in case JSON escapes were used in the response.
    def redact(value: str) -> str:
        if key:
            value = value.replace(key, "[REDACTED]")
            value = value.replace(json.dumps(key)[1:-1], "[REDACTED]")
        value = re.sub(r"\bxai-[A-Za-z0-9_-]+", "[REDACTED]", value, flags=re.IGNORECASE)
        value = re.sub(r"\bBearer\s+[^\s\"'<>]+", "Bearer [REDACTED]", value, flags=re.IGNORECASE)
        return re.sub(r"[\x00-\x1f\x7f]", " ", value).strip()
    raw = redact(raw)
    try:
        payload = json.loads(raw)
    except ValueError:
        detail = raw
    else:
        if isinstance(payload, dict):
            detail = payload.get("error", payload.get("message", payload))
            if isinstance(detail, dict):
                detail = {name: detail[name] for name in ("code", "type", "message", "detail") if name in detail} or detail
        else:
            detail = payload
        if not isinstance(detail, str):
            detail = json.dumps(detail, ensure_ascii=True)
    return redact(detail)[:2400] or "The server returned no error detail."


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify server-side xAI credentials, model access and a real Grok reply.")
    parser.add_argument("--list-models", action="store_true", help="List account-visible model IDs without generating a reply.")
    args = parser.parse_args()
    key = os.environ.get("XAI_API_KEY", "").strip()
    model = os.environ.get("XAI_MODEL", "").strip()
    if not key:
        print("XAI_API_KEY is missing. Set it in the Python server environment; never put it in UE assets.", file=sys.stderr)
        return 2
    if (key.lower().startswith("bearer ") or key.startswith(("'", '"'))
            or key.endswith(("'", '"')) or key == "System.Security.SecureString"
            or any(char.isspace() or not 33 <= ord(char) <= 126 for char in key)):
        print("XAI_API_KEY has an invalid local format. Paste only the full API key, without quotes, Bearer, spaces or line breaks.", file=sys.stderr)
        return 2
    if not args.list_models and not model:
        print("XAI_MODEL is missing. Use --list-models to inspect available model IDs.", file=sys.stderr)
        return 2
    try:
        request = Request("https://api.x.ai/v1/models", method="GET", headers={
            "Authorization": f"Bearer {key}",
            "Accept": "application/json",
            "User-Agent": "VFX-COSMO/0.2",
        })
        with urlopen(request, timeout=15) as response:
            models = sorted(item["id"] for item in json.load(response)["data"])
        if args.list_models:
            print(json.dumps({"models": models}, ensure_ascii=True))
            return 0
        if model not in models:
            print("XAI_MODEL is absent from this account's model list. Select an ID from --list-models.", file=sys.stderr)
            return 2
        os.environ["COSMO_AI_PROVIDER"] = "grok"
        from companion.session import CompanionSession
        session = CompanionSession()
        before = session.tutorial.get_status().to_dict()
        reply = session.handle_player_speech("Which four combat abilities does this game support? Answer briefly.")
        if reply.get("source") != "grok" or not reply.get("line") or reply.get("provider_error"):
            print("Real Grok acceptance failed: " + reply.get("provider_error", "missing source:grok or reply"), file=sys.stderr)
            return 1
        after = session.tutorial.get_status().to_dict()
        progress_fields = ("current_stage_id", "completed_stage_ids", "expected_event", "is_complete")
        if any(after[field] != before[field] for field in progress_fields) or reply.get("advanced"):
            print("Acceptance failed: player speech changed tutorial progress.", file=sys.stderr)
            return 1
        print(json.dumps({"source": reply["source"], "model": model, "line": reply["line"], "advanced": False}, ensure_ascii=True))
        return 0
    except HTTPError as exc:
        print(f"xAI HTTP {exc.code}.", file=sys.stderr)
        print("Server detail (credentials redacted): " + safe_error_detail(exc, key), file=sys.stderr)
        if args.list_models:
            print("This GET /v1/models request does not use XAI_MODEL or generate a model reply.", file=sys.stderr)
    except (URLError, TimeoutError, OSError, ValueError, KeyError, TypeError):
        print("xAI check failed: network unavailable or invalid API response. No credentials were logged.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
