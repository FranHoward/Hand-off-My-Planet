from __future__ import annotations

import argparse
import json
import sys

from companion.http_api import serve
from companion.session import CompanionSession

SESSION = CompanionSession()


def _print(payload: object) -> None:
    print(json.dumps(payload, indent=2))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="COSMO tutorial mock")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="Show tutorial + COSMO snapshot")
    sub.add_parser("open", help="Speak the first tutorial instruction")

    ev = sub.add_parser("event", help="Inject a gameplay event from Unreal/mock")
    ev.add_argument("name")

    say = sub.add_parser("say", help="Player voice line (does not advance stages)")
    say.add_argument("text", nargs="+")

    sub.add_parser("hint", help="Call get_available_tutorial_hint")
    sub.add_parser("step", help="Call get_current_tutorial_step")

    http = sub.add_parser("serve", help="HTTP mock API for Unreal / manual testing")
    http.add_argument("--host", default="127.0.0.1")
    http.add_argument("--port", type=int, default=8765)
    http.add_argument("--voice-port", type=int, default=0, help="Loopback Grok Voice relay port; 0 disables it")

    args = parser.parse_args(argv)

    if args.command == "serve":
        serve(args.host, args.port, args.voice_port)
        return 0
    if args.command == "open":
        _print(SESSION.open().to_dict())
        return 0
    if args.command == "status":
        _print(SESSION.snapshot())
        return 0
    if args.command == "event":
        _print(SESSION.ingest_game_event(args.name))
        return 0
    if args.command == "say":
        _print(SESSION.handle_player_speech(" ".join(args.text)))
        return 0
    if args.command == "hint":
        _print(SESSION.functions.call("get_available_tutorial_hint").result)
        return 0
    if args.command == "step":
        _print(SESSION.functions.call("get_current_tutorial_step").result)
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
