#!/usr/bin/env python3
"""Hold the QEMU serial socket (only one client allowed) and expose a
control socket so other processes can send commands / read responses."""
import socket, os, threading

SER = "/Users/herbcheng/venus-stack/alp.sock"
CTL = "/Users/herbcheng/venus-stack/alpctl.sock"
OUT = "/Users/herbcheng/venus-stack/alp_live.log"

ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
ser.connect(SER)
ser.settimeout(0.2)
log = open(OUT, "ab", 0)
log_lock = threading.Lock()

def reader():
    while True:
        try:
            data = ser.recv(65536)
            if not data:
                break
            with log_lock:
                log.write(data)
        except socket.timeout:
            pass
        except OSError:
            break

threading.Thread(target=reader, daemon=True).start()

if os.path.exists(CTL):
    os.unlink(CTL)
ctl_srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
ctl_srv.bind(CTL)
ctl_srv.listen(4)
while True:
    c, _ = ctl_srv.accept()
    c.settimeout(6.0)
    try:
        line = b""
        while not line.endswith(b"\n"):
            chunk = c.recv(4096)
            if not chunk:
                break
            line += chunk
        for b in line:
            ser.sendall(bytes([b]))
            __import__('time').sleep(0.008)
        deadline = __import__("time").time() + 5.0
        resp = b""
        while __import__("time").time() < deadline:
            try:
                data = ser.recv(65536)
                if not data:
                    break
                resp += data
                if resp.rstrip().endswith(b"#") or b"# " in resp[-8:]:
                    break
            except socket.timeout:
                continue
        c.sendall(resp)
    except OSError:
        pass
    finally:
        c.close()
