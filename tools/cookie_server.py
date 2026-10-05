#!/usr/bin/env python3
"""Tiny cookie test server for zero-browser.

GET /set-cookie -> Set-Cookie: zero_test=hello; Path=/
GET /echo       -> echoes the Cookie request header in plain text
GET /set-auth   -> sets a secure-looking/httponly cookie for later checks
"""
import http.server
import sys


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/set-cookie"):
            self.send_response(200)
            self.send_header("Set-Cookie", "zero_test=hello; Path=/")
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"cookie-set\n")
            return
        if self.path.startswith("/set-auth"):
            self.send_response(200)
            self.send_header("Set-Cookie", "auth=abc123; Path=/; HttpOnly")
            self.send_header("Set-Cookie", "pref=dark; Path=/; Max-Age=3600")
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"auth-set\n")
            return
        if self.path.startswith("/echo"):
            cookie = self.headers.get("Cookie", "")
            body = ("Cookie: " + cookie + "\n") if cookie else "no-cookie\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(body.encode("utf-8"))
            return
        self.send_response(404)
        self.end_headers()
        self.wfile.write(b"not-found\n")

    def log_message(self, fmt, *args):
        sys.stderr.write("[cookie_server] " + (fmt % args) + "\n")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8899
    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
