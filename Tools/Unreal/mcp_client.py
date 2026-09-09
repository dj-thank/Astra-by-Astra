"""Small loopback-only CLI for Unreal 5.8's native Streamable HTTP MCP server."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from urllib.parse import urlsplit
import requests


class UnrealMCP:
    def __init__(self, url="http://127.0.0.1:39258/star/mcp", timeout=120):
        parts = urlsplit(url)
        if parts.scheme != "http" or parts.hostname not in ("127.0.0.1", "localhost", "::1"):
            raise ValueError("Only the local Unreal MCP server is allowed")
        self.url, self.timeout, self.sequence = url, timeout, 0
        self.session = requests.Session()
        self.session.trust_env = False
        self.session.headers.update({"Accept": "application/json, text/event-stream"})

    def request(self, method, params=None, notification=False):
        self.sequence += 1
        payload = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        if not notification:
            payload["id"] = self.sequence
        response = self.session.post(self.url, json=payload, timeout=(5, self.timeout), allow_redirects=False)
        response.raise_for_status()
        if response.headers.get("Mcp-Session-Id"):
            self.session.headers["Mcp-Session-Id"] = response.headers["Mcp-Session-Id"]
        if not response.content:
            return None
        if "text/event-stream" in response.headers.get("Content-Type", ""):
            messages = [json.loads(line[5:].strip()) for line in response.text.splitlines() if line.startswith("data:")]
            data = next((m for m in messages if m.get("id") == self.sequence), None)
            if data is None:
                raise RuntimeError("No matching MCP response")
        else:
            data = response.json()
        if "error" in data:
            raise RuntimeError(json.dumps(data["error"], ensure_ascii=False))
        return data.get("result")

    def initialize(self):
        result = self.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "STAR-local-authoring", "version": "1.0"}})
        self.session.headers["MCP-Protocol-Version"] = result["protocolVersion"]
        self.request("notifications/initialized", notification=True)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("method", choices=("initialize", "tools/list", "tools/call"))
    parser.add_argument("--params", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    client = UnrealMCP(timeout=args.timeout)
    try:
        result = client.initialize()
        if args.method != "initialize":
            params = json.loads(args.params.read_text(encoding="utf-8")) if args.params else {}
            result = client.request(args.method, params)
        text = json.dumps(result, ensure_ascii=False, indent=2)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text + "\n", encoding="utf-8")
        else:
            print(text)
    finally:
        client.session.close()


if __name__ == "__main__":
    main()
