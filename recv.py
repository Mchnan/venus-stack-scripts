import socket, os, sys, threading
os.chdir('/Users/herbcheng/venus-stack/pull')
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', 8001)); srv.listen(8)
print('listening', flush=True)
def handle(c, addr):
    try:
        hdr = b''
        while b'\r\n\r\n' not in hdr:
            d = c.recv(4096)
            if not d: return
            hdr += d
        head, _, rest = hdr.partition(b'\r\n\r\n')
        line = head.split(b'\r\n')[0].decode()
        if 'POST' in line or 'PUT' in line:
            length = 0
            for h in head.split(b'\r\n'):
                if h.lower().startswith(b'content-length:'):
                    length = int(h.split(b':')[1])
            body = rest
            while len(body) < length:
                body += c.recv(65536)
            name = line.split(' ')[1].strip('/') or 'post.bin'
            open(name if name else 'post.bin', 'wb').write(body)
            c.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok')
        else:
            c.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nhi')
    except Exception as e:
        print('err', e, flush=True)
    finally:
        c.close()
while True:
    c, a = srv.accept()
    threading.Thread(target=handle, args=(c, a), daemon=True).start()
