#!/usr/bin/env python3
# TCP endpoint for MAME's modem-port bitbanger: logs everything the SE/30
# sends to /work/serial.log and sends the contents of /work/ctl/serial_in
# (deleted after sending) back to it.
import os, socket, time
IN = "/work/ctl/serial_in"
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 2323)); srv.listen(1)
conn, _ = srv.accept(); conn.setblocking(False)
log = open("/work/serial.log", "ab", buffering=0)
while True:
    try:
        data = conn.recv(4096)
        if not data:
            break
        log.write(data)
    except BlockingIOError:
        pass
    if os.path.exists(IN):
        payload = open(IN, "rb").read(); os.remove(IN)
        for b in payload:                    # pace it like a 9600-baud typist
            conn.send(bytes([b])); time.sleep(0.01)
    time.sleep(0.02)
