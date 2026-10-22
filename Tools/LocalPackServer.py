#!/usr/bin/env python3
"""Local stand-in for the `chordengine-packs` Supabase edge function.

Development and test use only - it never talks to Music-Prod. It serves the
pack JSON files in Resources/Packs with the same wire contract as
Backend/supabase/functions/chordengine-packs/index.ts:

  GET  /functions/v1/chordengine-packs?action=catalog
  POST /functions/v1/chordengine-packs  {"action":"download","token":"...","slug":"..."}
         member-token     -> 200 {ok, slug, version, sha256, download_url}
         nonmember-token  -> 403 {error: "membership_required", unlock_url, unlock_message}
         anything else    -> 401 {error: "unauthorized"}
         unknown slug     -> 404 {error: "not_found"}
  GET  /files/<slug>.json?expires=<unix>&sig=<hex>   (the "signed" download URL)

Usage (from the repository root):
  python3 Tools/LocalPackServer.py --port 8765
  python3 Tools/LocalPackServer.py --port 0          # pick a free port
  python3 Tools/LocalPackServer.py --tamper          # serve altered bytes (sha mismatch)

On start it prints "LISTENING <port>" so scripts can wait for it. It binds
127.0.0.1 only. Standard library only.
"""

import argparse
import hashlib
import hmac
import json
import os
import secrets
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

FUNCTION_PATH = "/functions/v1/chordengine-packs"
STOCK_SLUG = "chordengine-stock"
SIGNED_URL_TTL_SECONDS = 600
UNLOCK_URL = "https://music-prod.com/plus"
UNLOCK_MESSAGE = "Preset packs are included with Music-Prod+. Join at music-prod.com/plus"
MAX_BODY_BYTES = 64 * 1024
SORT_ORDER = {STOCK_SLUG: 10, "chordengine-test-pack": 20}


def load_packs(packs_dir):
    """slug -> {meta..., bytes} for every valid-looking pack file."""
    packs = {}
    for name in sorted(os.listdir(packs_dir)):
        if not name.endswith(".json"):
            continue
        with open(os.path.join(packs_dir, name), "rb") as handle:
            data = handle.read()
        try:
            doc = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            print(f"skipping {name}: not JSON", file=sys.stderr)
            continue
        if doc.get("format") != "chordengine-pack" or name != f"{doc.get('slug')}.json":
            print(f"skipping {name}: not a pack named after its slug", file=sys.stderr)
            continue
        slug = doc["slug"]
        packs[slug] = {
            "slug": slug,
            "name": doc.get("name", slug),
            "description": doc.get("description", ""),
            "version": doc.get("version", "1.0.0"),
            "preset_count": len(doc.get("presets", [])),
            "is_stock": slug == STOCK_SLUG,
            "size_bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "sort_order": SORT_ORDER.get(slug, 100),
            "bytes": data,
        }
    return packs


class Handler(BaseHTTPRequestHandler):
    server_version = "LocalPackServer/1"

    # -------------------------------------------------------------- helpers
    def send_json(self, status, body):
        payload = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def sign(self, slug, expires):
        message = f"{slug}:{expires}".encode("utf-8")
        return hmac.new(self.server.signing_key, message, hashlib.sha256).hexdigest()

    def log_message(self, fmt, *args):  # quieter, one line per request
        sys.stderr.write("[LocalPackServer] " + (fmt % args) + "\n")

    # ------------------------------------------------------------------ GET
    def do_GET(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)

        if url.path == FUNCTION_PATH and query.get("action", [""])[0] == "catalog":
            packs = sorted(self.server.packs.values(), key=lambda p: (p["sort_order"], p["name"]))
            listing = [{k: v for k, v in p.items() if k != "bytes"} for p in packs]
            return self.send_json(200, {
                "version": 1,
                "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "packs": listing,
            })

        if url.path.startswith("/files/") and url.path.endswith(".json"):
            slug = url.path[len("/files/"):-len(".json")]
            pack = self.server.packs.get(slug)
            expires = query.get("expires", ["0"])[0]
            sig = query.get("sig", [""])[0]
            if pack is None or not expires.isdigit():
                return self.send_json(404, {"error": "not_found"})
            if int(expires) < time.time() or not hmac.compare_digest(sig, self.sign(slug, expires)):
                return self.send_json(400, {"error": "invalid_signature"})
            data = pack["bytes"]
            if self.server.tamper:
                data = data.replace(b"Music-Prod", b"Music-Pr0d", 1)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return None

        return self.send_json(404, {"error": "not_found"})

    # ----------------------------------------------------------------- POST
    def do_POST(self):
        if urlparse(self.path).path != FUNCTION_PATH:
            return self.send_json(404, {"error": "not_found"})

        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BODY_BYTES:
            return self.send_json(413, {"error": "too_large"})
        try:
            payload = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
        except (UnicodeDecodeError, ValueError):
            return self.send_json(400, {"error": "invalid_request"})
        if not isinstance(payload, dict):
            return self.send_json(400, {"error": "invalid_request"})

        action = payload.get("action")
        if action == "catalog":
            self.path = FUNCTION_PATH + "?action=catalog"
            return self.do_GET()
        if action != "download":
            return self.send_json(400, {"error": "unknown_action"})

        # Same order as the real function: identity, then membership, then pack.
        token = payload.get("token")
        if token == "nonmember-token":
            return self.send_json(403, {
                "error": "membership_required",
                "unlock_url": UNLOCK_URL,
                "unlock_message": UNLOCK_MESSAGE,
            })
        if token != "member-token":
            return self.send_json(401, {"error": "unauthorized"})

        pack = self.server.packs.get(str(payload.get("slug", "")))
        if pack is None:
            return self.send_json(404, {"error": "not_found"})

        expires = str(int(time.time()) + SIGNED_URL_TTL_SECONDS)
        host, port = self.server.server_address[:2]
        download_url = (f"http://{host}:{port}/files/{pack['slug']}.json"
                        f"?expires={expires}&sig={self.sign(pack['slug'], expires)}")
        return self.send_json(200, {
            "ok": True,
            "slug": pack["slug"],
            "version": pack["version"],
            "sha256": pack["sha256"],
            "download_url": download_url,
        })


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8765, help="0 picks a free port")
    parser.add_argument("--packs-dir", default=os.path.join(here, "..", "Resources", "Packs"))
    parser.add_argument("--tamper", action="store_true",
                        help="serve altered bytes so the client must reject the checksum")
    args = parser.parse_args()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.packs = load_packs(os.path.abspath(args.packs_dir))
    server.signing_key = secrets.token_bytes(32)
    server.tamper = args.tamper
    print(f"LISTENING {server.server_address[1]}", flush=True)
    for pack in server.packs.values():
        print(f"  {pack['slug']} {pack['version']} {pack['size_bytes']} B sha256={pack['sha256']}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
