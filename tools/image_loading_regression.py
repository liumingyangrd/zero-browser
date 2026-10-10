"""Check resource discovery, parallel downloads, and painted images using --shot.

Usage: python tools/image_loading_regression.py build/zero-browser.exe
Requires only Python's standard library. All fixtures and output stay in build/.
"""
import collections
import http.server
from pathlib import Path
import re
import struct
import subprocess
import sys
import threading


def main():
    exe = Path(sys.argv[1]).resolve()
    output = Path('build') / ('image-regression-' + exe.stem)
    output.mkdir(parents=True, exist_ok=True)
    # WIC can decode BMP bytes independently of the URL extension.
    pixels = bytes((0, 0, 255)) * 16 * 16
    bitmap = (struct.pack('<2sIHHI', b'BM', 54 + len(pixels), 0, 0, 54)
              + struct.pack('<IiiHHIIiiII', 40, 16, 16, 1, 24, 0,
                            len(pixels), 0, 0, 0, 0) + pixels)
    expected = {'/img%d.bmp' % i for i in range(32)}
    expected.update(('/head.bmp', '/body.bmp', '/inline.bmp', '/entity.bmp?a=1&b=2'))
    html = '''<!doctype html><html><head><style>
    /* url('/fake-comment.bmp') */
    .head {width:32px;height:32px;background-image:URL( '/head.bmp' );}
    </style><script>
    var fakeStyle = "<style>.fake{background:url('/fake-script.bmp')}</style>";
    var fakeImg = '<img src="/fake-img.bmp">';
    </script></head><body>
    <!-- <img src="/fake-html-comment.bmp"> -->
    <div class="head"></div>
    <style>.body {width:32px;height:32px;background-image:url('/body.bmp')}</style>
    <div class="body"></div>
    <div style="width:32px;height:32px;background-image:url('/inline.bmp')"></div>
    <div style="width:32px;height:32px;background-image:url('/entity.bmp?a=1&amp;b=2')"></div>
    ''' + ''.join('<img width="16" height="16" src="/img%d.bmp">' % i
                  for i in range(32)) + '''
    <img width="16" height="16" data-src="/img0.bmp">
    </body></html>'''
    requests = collections.Counter()
    lock = threading.Lock()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            with lock:
                requests[self.path] += 1
            if self.path == '/':
                body, kind, status = html.encode(), 'text/html; charset=utf-8', 200
            elif self.path in expected:
                body, kind, status = bitmap, 'image/bmp', 200
            else:
                body, kind, status = b'Unexpected resource', 'text/plain', 404
            self.send_response(status)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        shot = output / 'shot.bmp'
        result = subprocess.run([str(exe), '--shot', '--url',
                                 'http://127.0.0.1:%d/' % server.server_port,
                                 '--out', str(shot), '--wait', '3000',
                                 '--size', '1000x800', '--set-lang', 'en',
                                 '--settings-file', str(output / 'settings.ini')],
                                capture_output=True, timeout=45)
        (output / 'output.log').write_bytes(result.stdout + result.stderr)
        assert result.returncode == 0, 'headless render failed'
        assert set(requests) == expected | {'/'}, 'incorrect URLs: %r' % requests
        assert all(requests[url] == 1 for url in expected), 'duplicate downloads'
        assert re.search(rb'images=\d+ms\(36\)', result.stdout), 'not all images decoded'
        data = shot.read_bytes()
        offset = struct.unpack_from('<I', data, 10)[0]
        width, height = struct.unpack_from('<ii', data, 18)
        bits = struct.unpack_from('<H', data, 28)[0]
        assert bits in (24, 32), 'unexpected screenshot format'
        stride = ((width * bits + 31) // 32) * 4
        red = 0
        for y in range(abs(height)):
            for x in range(width):
                start = offset + y * stride + x * (bits // 8)
                b, g, r = data[start:start + 3]
                red += r > 240 and g < 15 and b < 15
        assert red >= 32 * 16 * 16, 'decoded images missing from painted layout'
        print('PASS %s: 36 unique images decoded; no script/comment URLs; '
              '%d red pixels painted' % (exe.name, red))
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
