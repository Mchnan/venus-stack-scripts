#!/usr/bin/env python3
"""Send a command to the guest serial console via the holder's ctl socket."""
import socket, sys

CTL = "/Users/herbcheng/venus-stack/alpctl.sock"

def main():
    cmd = " ".join(sys.argv[1:])
    if not cmd:
        print("usage: ser.py command"); sys.exit(1)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(CTL)
    s.settimeout(6.0)
    s.sendall((cmd + "\n").encode())
    resp = b""
    try:
        while True:
            data = s.recv(65536)
            if not data:
                break
            resp += data
    except socket.timeout:
        pass
    s.close()
    sys.stdout.write(resp.decode("utf-8", "replace"))

if __name__ == "__main__":
    main()
