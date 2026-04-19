#!/usr/bin/env python3
"""Persistent serial logger — opens the port ONCE and streams to a file.

The CH340 asserts DTR/RTS on port-open, which resets the ESP8266. So every
fresh open reboots the board. This logger opens once and keeps the handle, so
the board resets a single time at startup and then runs uninterrupted while we
tail/grep the log file (no further port opens, no further resets).

Single owner: while this runs, do NOT run idf.py monitor or any other serial
reader on the same port.

Usage: serial_logger.py <port> <logfile>
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-210"
logfile = sys.argv[2] if len(sys.argv) > 2 else "/tmp/einktodo-serial.log"


def open_port():
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 1
    # Keep reset lines deasserted; minimizes (cannot fully avoid) the open pulse.
    s.dtr = False
    s.rts = False
    s.open()
    return s


with open(logfile, "a", buffering=1) as f:
    f.write("\n===== logger start %s =====\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    while True:
        try:
            s = open_port()
        except Exception as e:  # port gone (USB re-enumerate) — wait and retry
            f.write("[logger] open failed: %s — retry in 2s\n" % e)
            time.sleep(2)
            continue
        f.write("[logger] port opened %s\n" % time.strftime("%H:%M:%S"))
        try:
            while True:
                data = s.read(4096)
                if data:
                    text = data.decode("utf-8", "replace")
                    for line in text.splitlines(keepends=True):
                        if line.strip():
                            f.write(line if line.endswith("\n") else line + "\n")
        except Exception as e:
            f.write("[logger] read error: %s — reopening\n" % e)
            try:
                s.close()
            except Exception:
                pass
            time.sleep(2)
