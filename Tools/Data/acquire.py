"""Acquire pinned public source bytes into project-local work/raw; never require keys."""
from __future__ import annotations
import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import time
import requests

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "work" / "raw"

def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""): h.update(chunk)
    return h.hexdigest()

def save_json(path, data):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8", newline="\n")

def acquire(item):
    RAW.mkdir(parents=True, exist_ok=True)
    path = RAW / item["filename"]
    receipt = path.with_suffix(path.suffix + ".receipt.json")
    if path.exists() and not receipt.exists():
        raise RuntimeError(f"Unreceipted existing cache retained; use a separate source filename or inspect it: {path}")
    if path.exists() and receipt.exists():
        data = json.loads(receipt.read_text(encoding="utf-8"))
        if data["sourceUrl"] == item["url"] and digest(path) == data["sha256"]:
            if item.get('sha256') and data['sha256'] != item['sha256']:
                raise RuntimeError('Cached source differs from pinned catalog digest')
            print("CACHED", item["id"], flush=True); return data
        raise RuntimeError(f"Existing source changed; preserve and inspect: {path}")
    partial = path.with_suffix(path.suffix + ".partial")
    if partial.exists():
        raise RuntimeError(f"Interrupted download retained for inspection: {partial}")
    print("FETCH", item["id"], item["url"], flush=True)
    # Deliberately no retries after denied responses. Use ordinary official distribution.
    with requests.get(item["url"], stream=True, timeout=(20, 90)) as response:
        response.raise_for_status()
        length = int(response.headers.get("Content-Length", 0))
        if length > 2_000_000_000: raise RuntimeError("Per-file source size exceeds 2GB budget")
        size = 0
        with partial.open("wb") as f:
            for chunk in response.iter_content(1024 * 1024):
                size += len(chunk)
                if size > 2_000_000_000: raise RuntimeError("Download exceeded 2GB budget")
                f.write(chunk)
        # requests iter_content yields decoded bytes; Content-Length describes compressed transfer bytes.
        if length and not response.headers.get('Content-Encoding') and size != length: raise RuntimeError("Truncated source")
        sha256=digest(partial)
        if item.get('sha256') and item['sha256'] != sha256:
            raise RuntimeError('Downloaded source differs from pinned catalog digest; retained partial for inspection')
        data = {"id": item["id"], "sourceUrl": item["url"], "resolvedUrl": response.url,
                "retrievedAt": dt.datetime.now(dt.timezone.utc).isoformat(), "bytes": size,
                "sha256": sha256, "lastModified": response.headers.get("Last-Modified"),
                "transferContentEncoding": response.headers.get('Content-Encoding','identity'),
                "contentType": response.headers.get("Content-Type")}
    partial.rename(path)
    save_json(receipt, data)
    print("DONE", item["id"], size, flush=True)
    return data

def main():
    parser = argparse.ArgumentParser(); parser.add_argument("--only", nargs="*"); args = parser.parse_args()
    catalog = json.loads((ROOT / "Tools/Data/sources.json").read_text(encoding="utf-8"))
    items = [x for x in catalog if not args.only or x["id"] in args.only]
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        jobs = {pool.submit(acquire, x): x for x in items}
        for f in concurrent.futures.as_completed(jobs):
            try: f.result()
            except Exception as e:
                failures.append({"id": jobs[f]["id"], "url": jobs[f]["url"], "error": str(e)})
                print("FAILED", failures[-1], flush=True)
    save_json(ROOT / "work/acquisition-result.json", {"failures": failures})
    if failures: raise SystemExit(1)

if __name__ == "__main__": main()
