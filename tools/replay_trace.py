"""Open a recorded JSONL trace in the standalone browser player."""

import argparse
import json
import threading
import webbrowser
from html import escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from socketserver import TCPServer

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT.parent / "web"


class ReplayHTTPServer(ThreadingHTTPServer):
    def server_bind(self):
        TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address


def load_trace(path, summary_path=None):
    events = []
    with path.open(encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"line {number}: invalid JSON: {exc.msg}") from exc
            if (
                not isinstance(event, dict)
                or event.get("schema") != 1
                or not isinstance(event.get("tick"), int)
                or event["tick"] < 0
                or not isinstance(event.get("kind"), str)
            ):
                raise ValueError(f"line {number}: unsupported trace event")
            if events and event["tick"] < events[-1]["tick"]:
                raise ValueError(f"line {number}: simulation time decreases")
            events.append(event)
    if not events:
        raise ValueError("trace contains no events")
    candidates = (
        [summary_path]
        if summary_path
        else [path.with_suffix(".summary.json"), path.with_suffix(".json")]
    )
    summary = {}
    for candidate in candidates:
        if not candidate.is_file():
            if summary_path:
                raise ValueError(f"summary does not exist: {candidate}")
            continue
        try:
            summary = json.loads(candidate.read_text(encoding="utf-8"))
            if not isinstance(summary, dict):
                raise ValueError("expected a JSON object")
        except ValueError as exc:
            raise ValueError(f"invalid summary {candidate}: {exc}") from exc
        break
    return {
        "schema": 1,
        "examples": [
            {
                "name": path.name,
                "program": "",
                "configuration": summary.get("configuration"),
                "cores": summary.get("cores"),
                "backend": summary.get("backend"),
                "events": events,
            }
        ],
    }


def serve(path, bundle, open_browser=True):
    page = (ROOT / "trace_replay.html").read_bytes()
    page = page.replace(
        b'<p id="trace-file"></p>', ('<p id="trace-file">' + escape(str(path)) + "</p>").encode()
    )
    payloads = {
        "/": ("text/html; charset=utf-8", page),
        "/trace.json": ("application/json; charset=utf-8", json.dumps(bundle).encode()),
    }

    for asset in ASSETS.rglob("*"):
        if asset.suffix not in {".js", ".css", ".woff2"}:
            continue
        mime = {".js": "text/javascript", ".css": "text/css", ".woff2": "font/woff2"}[asset.suffix]
        payloads["/" + asset.relative_to(ASSETS).as_posix()] = (mime, asset.read_bytes())

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            item = payloads.get(self.path)
            if item is None:
                self.send_error(404)
                return
            content_type, body = item
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ReplayHTTPServer(("127.0.0.1", 0), Handler)
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"Trace replay: {url}", flush=True)
    if open_browser:
        threading.Timer(0.1, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="JSONL trace to replay")
    parser.add_argument(
        "--no-browser", action="store_true", help="print URL without opening a browser"
    )
    parser.add_argument(
        "--summary", type=Path, help="JSON run summary containing core configurations"
    )
    args = parser.parse_args()
    try:
        path = args.trace.expanduser().resolve(strict=True)
        bundle = load_trace(path, args.summary)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    serve(path, bundle, not args.no_browser)


if __name__ == "__main__":
    main()
