#!/usr/bin/env -S uv run --script
# /// script
# dependencies = ["pyserial"]
# ///
import argparse
import os
import sys
import threading
import time

import serial


def open_port(path):
    port = serial.Serial()
    port.port = path
    port.baudrate = 115200
    port.dtr = False
    port.rts = False
    port.timeout = 0.5
    port.open()
    return port


class Link:
    def __init__(self, path, log_path):
        self.path = path
        self.log = open(log_path, "a", buffering=1, encoding="utf-8", errors="replace")
        self.port = None
        self.lock = threading.Lock()

    def ensure_open(self):
        with self.lock:
            if self.port is None or not self.port.is_open:
                try:
                    self.port = open_port(self.path)
                    self.log.write(f"#link connected {time.strftime('%H:%M:%S')}\n")
                except serial.SerialException:
                    self.port = None
            return self.port

    def drop(self):
        with self.lock:
            if self.port is not None:
                try:
                    self.port.close()
                except Exception:
                    pass
            self.port = None
            self.log.write(f"#link disconnected {time.strftime('%H:%M:%S')}\n")

    def reader(self):
        pending = b""
        while True:
            port = self.ensure_open()
            if port is None:
                time.sleep(1)
                continue
            try:
                data = port.read(4096)
            except serial.SerialException:
                self.drop()
                continue
            if not data:
                continue
            pending += data
            *lines, pending = pending.split(b"\n")
            for line in lines:
                self.log.write(line.decode("utf-8", "replace").rstrip("\r") + "\n")

    def send(self, command):
        port = self.ensure_open()
        if port is None:
            self.log.write(f"#send failed, not connected: {command}\n")
            return
        try:
            port.write((command.strip() + "\n").encode())
            self.log.write(f"#sent {command.strip()}\n")
        except serial.SerialException:
            self.drop()


def main():
    parser = argparse.ArgumentParser(description="Bidirectional serial link to kikite")
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--fifo", required=True, help="named pipe; each line written to it is sent as a command")
    args = parser.parse_args()

    if not os.path.exists(args.fifo):
        os.mkfifo(args.fifo)
    link = Link(args.port, args.log)
    threading.Thread(target=link.reader, daemon=True).start()
    while True:
        with open(args.fifo, "r") as fifo:
            for line in fifo:
                if line.strip():
                    link.send(line)


if __name__ == "__main__":
    sys.exit(main())
