import http.server, datetime, os
MIRROR = '/Users/herbcheng/venus-stack/mirror'
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=MIRROR, **k)
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(n) if n else b''
        name = self.path.strip('/').replace('/', '_') or 'post'
        out = f"/tmp/pulled/{name}.{datetime.datetime.now().strftime('%H%M%S')}"
        open(out, 'wb').write(body)
        self.send_response(200); self.send_header('Content-Length','2'); self.end_headers()
        self.wfile.write(b'ok')
    def log_message(self, *a): pass
os.chdir(MIRROR)
http.server.HTTPServer(('0.0.0.0', 8000), H).serve_forever()
