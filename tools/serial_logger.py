#!/usr/bin/env python3
"""Append ESP32 serial output to a timestamped log and reconnect on USB resets."""

from __future__ import annotations

import argparse
import os
import time
from datetime import datetime
from pathlib import Path

import serial


MAX_LOG_BYTES = 16 * 1024 * 1024


def timestamp() -> str:
    return datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def rotate_if_needed(log_path: Path) -> None:
    if not log_path.exists() or log_path.stat().st_size < MAX_LOG_BYTES:
        return
    backup_path = log_path.with_suffix(log_path.suffix + ".1")
    os.replace(log_path, backup_path)


def write_line(log_file, line: str) -> None:
    log_file.write(f"[{timestamp()}] {line}\n")
    log_file.flush()


def run(port: str, baud: int, log_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    buffered = b""
    while True:
        rotate_if_needed(log_path)
        with log_path.open("a", encoding="utf-8", buffering=1) as log_file:
            try:
                # Set modem-control lines before opening: their default asserted
                # state can reset the P4 into its USB download mode.
                device = serial.Serial(port=None, baudrate=baud, timeout=1)
                device.dtr = False
                device.rts = False
                device.port = port
                device.open()
                with device:
                    write_line(log_file, f"serial connected: {port} @ {baud}")
                    buffered = b""
                    while True:
                        chunk = device.read(device.in_waiting or 1)
                        if not chunk:
                            continue
                        buffered += chunk
                        lines = buffered.split(b"\n")
                        buffered = lines.pop()
                        for line in lines:
                            write_line(log_file, line.rstrip(b"\r").decode("utf-8", errors="replace"))
            except (serial.SerialException, OSError) as error:
                write_line(log_file, f"serial disconnected: {error}")
                time.sleep(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--log", type=Path, default=Path("logs/serial.log"))
    args = parser.parse_args()
    run(args.port, args.baud, args.log)


if __name__ == "__main__":
    main()
