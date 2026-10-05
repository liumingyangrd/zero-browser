"""极小的回显服务器：把收到的请求头和 Cookie 原样返回，用于验证 UA / Referer / Cookie。

用法: python tools/echo_headers.py [端口]
"""
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        lines = ["GET %s" % self.path]
        for k, v in self.headers.items():
            lines.append("%s: %s" % (k, v))
        body = "\n".join(lines).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8901
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()
