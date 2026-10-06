"""表单探针服务器：给自研表单交互做端到端取证。

- GET/POST /echo ：把请求方法、查询串、Content-Type、请求体和解析出的字段
  渲染成一个 HTML 页面。浏览器提交之后，页面上会直接出现服务端收到的内容，
  于是 `--shot --dump-boxes` 就能把"提交到底发出去了什么"取证下来。
- 其余路径     ：把 testpage/ 当静态目录发（与 python -m http.server 一致）。

用法: python tools/form_server.py [端口]     默认 8902

配合 testpage/formsubmit.html 使用：那页里 POST 表单与 GET 表单的 action
都指向 /echo。
"""
import os
import sys
import urllib.parse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "testpage")
)


def esc(text):
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def _echo(self, method):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length > 0 else b""
        body = raw.decode("utf-8", "replace")
        query = urllib.parse.urlsplit(self.path).query
        pairs = urllib.parse.parse_qsl(query, keep_blank_values=True)
        if body:
            pairs += urllib.parse.parse_qsl(body, keep_blank_values=True)
        rows = "".join(
            "<li>%s = %s</li>" % (esc(k), esc(v)) for k, v in pairs
        )
        html = (
            "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">"
            "<title>echo</title></head><body>\n"
            "<h1>表单回显</h1>\n"
            "<p>method=%s</p>\n"
            "<p>query=%s</p>\n"
            "<p>content-type=%s</p>\n"
            "<p>body=%s</p>\n"
            "<p>fields=%d</p>\n"
            "<ul>%s</ul>\n"
            "</body></html>\n"
            % (
                esc(method),
                esc(query),
                esc(self.headers.get("Content-Type") or "-"),
                esc(body),
                len(pairs),
                rows,
            )
        )
        data = html.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if urllib.parse.urlsplit(self.path).path == "/echo":
            self._echo("GET")
        else:
            super().do_GET()

    def do_POST(self):
        if urllib.parse.urlsplit(self.path).path == "/echo":
            self._echo("POST")
        else:
            self.send_error(404, "only /echo accepts POST")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8902
    print("form_server: http://127.0.0.1:%d/  (root=%s)" % (port, ROOT))
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
