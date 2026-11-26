#!/usr/bin/env python3
"""Smoke-test the SG-4 editor host without third-party dependencies."""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request


def request(url: str, token: str | None, method: str = "GET", body: object | None = None):
    headers = {}
    if token:
        headers["X-SG-Token"] = token
    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        payload = error.read().decode("utf-8", errors="replace")
        try:
            return error.code, json.loads(payload)
        except json.JSONDecodeError:
            return error.code, payload


def request_bytes(url: str, token: str):
    req = urllib.request.Request(url, headers={"X-SG-Token": token}, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            return response.status, response.headers.get_content_type(), response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.headers.get_content_type(), error.read()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8777)
    parser.add_argument("--token", required=True)
    parser.add_argument("--commands", action="store_true", help="also apply one addNode command")
    parser.add_argument("--preview", action="store_true", help="compile and fetch one preview JPEG")
    parser.add_argument("--node-preview", action="store_true",
                        help="compile and fetch one node preview PNG")
    args = parser.parse_args()

    base = f"http://127.0.0.1:{args.port}"
    status, health = request(base + "/api/health", None)
    print("health", status, health)
    if status != 200:
        return 1

    status, session = request(base + "/api/session", args.token)
    print("session", status, session)
    if status != 200 or not session.get("ok"):
        return 1

    status, document = request(base + "/api/document", args.token)
    print("document", status, "revision", document.get("revision"), "nodes",
          len(document.get("data", {}).get("graph", {}).get("nodes", [])))
    if status != 200:
        return 1

    status, catalog = request(base + "/api/catalog/nodes?q=multiply&limit=5", args.token)
    print("catalog", status, catalog.get("data", {}).get("nodes", [])[:1])
    if status != 200:
        return 1

    if args.commands:
        revision = document.get("revision", 0)
        status, result = request(
            base + "/api/commands",
            args.token,
            "POST",
            {
                "baseRevision": revision,
                "commands": [
                    {
                        "op": "addNode",
                        "type": "Multiply",
                        "id": 9001,
                        "position": [10, 20],
                    }
                ],
            },
        )
        print("commands", status, result)
        if status != 200 or not result.get("ok"):
            return 1

        status, stale = request(
            base + "/api/commands",
            args.token,
            "POST",
            {"baseRevision": revision, "commands": []},
        )
        print("stale-revision", status, stale)
        if status != 409:
            return 1

        status, document = request(base + "/api/document", args.token)
        if status != 200:
            return 1

    if args.preview:
        status, compiled = request(
            base + "/api/compile",
            args.token,
            "POST",
            {"mode": "preview", "variants": "used"},
        )
        if status != 200 or not compiled.get("ok"):
            print("preview-compile-start", status, compiled)
            return 1
        job_id = compiled["data"]["jobId"]
        revision = compiled["data"].get("revision", document.get("revision", 0))
        job = None
        for _ in range(120):
            status, job = request(base + f"/api/compile/{job_id}", args.token)
            if status != 200 or job.get("data", {}).get("state") not in ("queued", "running"):
                break
            time.sleep(0.25)
        state = job.get("data", {}).get("state") if isinstance(job, dict) else None
        print("preview-compile", status, state, job.get("data", {}).get("stats") if isinstance(job, dict) else None)
        if status != 200 or state != "succeeded":
            return 1
        status, content_type, data = request_bytes(
            base + f"/api/preview.jpg?size=256&rev={revision}", args.token
        )
        print("preview", status, content_type, len(data), data[:2].hex())
        if status != 200 or content_type != "image/jpeg" or len(data) < 128 or data[:2] != b"\xff\xd8":
            return 1

    if args.node_preview:
        nodes = document.get("data", {}).get("graph", {}).get("nodes", [])
        node_id = next((node["id"] for node in nodes if not node.get("type", "").startswith("Block")), None)
        if node_id is None:
            print("node-preview: no previewable node")
            return 1
        revision = document.get("revision", 0)
        status = 0
        content_type = ""
        data = b""
        for _ in range(120):
            status, content_type, data = request_bytes(
                base + f"/api/preview/node/{node_id}.png?rev={revision}&size=128", args.token
            )
            if status == 200 or status not in (202, 503):
                break
            time.sleep(0.25)
        print("node-preview", status, content_type, len(data), data[:8].hex())
        if status != 200 or content_type != "image/png" or data[:8] != b"\x89PNG\r\n\x1a\n":
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
