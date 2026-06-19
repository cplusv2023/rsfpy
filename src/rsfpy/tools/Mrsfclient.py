#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Mrsfview_client.py

Runner for RSF SVG client.

Local GUI client:
    Mrsfview_client.py
    -> runs bundled bin/rsfclient

Remote sender:
    sfspike n1=10 | rsfgraph | Mrsfview_client.py --send

This Python entry no longer implements the client GUI/CLI itself.
Only --send is kept in Python so the remote side can send SVG data without
requiring the compiled GTK client binary.
"""

import os
import sys
import json
import time
import socket
import struct
import argparse
import subprocess
import importlib.resources
from pathlib import Path

from rsfpy import bin


APP_VERSION = "runner-send-v1-2026-06-19"
MAGIC = b"RSFVIEW2"
MAX_PAYLOAD = 1024 * 1024 * 1024


def now_string():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def format_msg(msg):
    return "[%s] %s" % (now_string(), msg)


def is_error_text(msg):
    s = str(msg).lower()
    keys = (
        "error",
        "failed",
        "fail",
        "exception",
        "bad ",
        "not found",
        "address already in use",
        "denied",
        "refused",
        "timeout",
    )
    return any(k in s for k in keys)


def is_success_text(msg):
    s = str(msg).lower()
    keys = (
        "success",
        "established successfully",
        "connected",
        "listening on",
        "saved svg",
        "received svg",
        "sent ",
    )
    return any(k in s for k in keys) and not is_error_text(s)


def ansi_color(msg):
    if is_error_text(msg):
        return "\033[1;31m%s\033[0m" % msg
    if is_success_text(msg):
        return "\033[1;32m%s\033[0m" % msg
    return msg


# ----------------------------------------------------------------------
# Sender mode: kept in Python
# ----------------------------------------------------------------------

def send_payload(host, port, token, data, meta):
    token_bytes = (token or "").encode("utf-8", errors="replace")
    meta_bytes = json.dumps(meta or {}, ensure_ascii=False).encode("utf-8")

    if len(token_bytes) > 65535:
        raise RuntimeError("token is too long")

    if len(data) > MAX_PAYLOAD:
        raise RuntimeError("payload too large: %d bytes" % len(data))

    with socket.create_connection((host, port), timeout=10.0) as sock:
        sock.sendall(MAGIC)
        sock.sendall(struct.pack("!H", len(token_bytes)))
        sock.sendall(token_bytes)
        sock.sendall(struct.pack("!I", len(meta_bytes)))
        sock.sendall(meta_bytes)
        sock.sendall(struct.pack("!Q", len(data)))
        sock.sendall(data)


def sender_main(argv):
    parser = argparse.ArgumentParser(
        prog=os.path.basename(sys.argv[0]) + " --send",
        description="Send SVG from stdin to rsfclient receiver.",
    )

    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17890)
    parser.add_argument("--token", default=os.environ.get("RSFVIEW_TOKEN", ""))
    parser.add_argument("--title", default="")

    ns = parser.parse_args(argv)

    data = sys.stdin.buffer.read()
    if not data:
        print(ansi_color(format_msg("error: empty stdin")), file=sys.stderr)
        return 1

    meta = {
        "title": ns.title,
        "time": now_string(),
    }

    try:
        send_payload(ns.host, ns.port, ns.token, data, meta)
    except Exception as e:
        print(ansi_color(format_msg("failed to send SVG: %s" % e)), file=sys.stderr)
        return 1

    print(ansi_color(format_msg("sent %d bytes" % len(data))), file=sys.stderr)
    return 0


# ----------------------------------------------------------------------
# Runner mode: delegate GUI/CLI to bundled bin/rsfclient
# ----------------------------------------------------------------------

def resource_path(name):
    return importlib.resources.files(bin).joinpath(name)


def is_executable(path):
    try:
        path = Path(path)
        if not path.is_file():
            return False

        if os.name == "nt":
            return True

        return os.access(str(path), os.X_OK)
    except Exception:
        return False


def candidate_rsfclient_paths():
    env = os.environ.get("RSFPY_RSFCLIENT_BIN", "")
    if env:
        yield Path(env)

    if os.name == "nt":
        yield resource_path("rsfclient.exe")
        yield resource_path("rsfclient")
    else:
        yield resource_path("rsfclient")
        yield resource_path("rsfclient.exe")


def find_rsfclient():
    for path in candidate_rsfclient_paths():
        if is_executable(path):
            return str(path)

    return ""


def run_bundled_client(argv):
    exe = find_rsfclient()

    if not exe:
        sys.stderr.write(
            "No bundled rsfclient executable found.\n"
            "Expected one of: bin/rsfclient or bin/rsfclient.exe.\n"
            "You can also set RSFPY_RSFCLIENT_BIN=/path/to/rsfclient.\n"
        )
        return 127

    try:
        result = subprocess.run([exe] + list(argv))
        return result.returncode
    except KeyboardInterrupt:
        return 130
    except Exception as e:
        sys.stderr.write("failed to run rsfclient: %s\n" % e)
        return 1


def print_help():
    prog = os.path.basename(sys.argv[0])
    text = f"""\
Usage:
    {prog}
        Run bundled bin/rsfclient GUI.

    {prog} [args...]
        Pass args to bundled bin/rsfclient.

    {prog} --send [--host 127.0.0.1] [--port 17890] [--token TOKEN] [--title TITLE] < figure.svg
        Send SVG from stdin to a running rsfclient receiver.

Environment:
    RSFPY_RSFCLIENT_BIN
        Override bundled rsfclient executable path.

    RSFVIEW_TOKEN
        Default token for --send mode.

Version:
    {APP_VERSION}
"""
    sys.stdout.write(text)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)

    if "--send" in argv:
        idx = argv.index("--send")
        return sender_main(argv[:idx] + argv[idx + 1:])

    if "--runner-version" in argv:
        print(APP_VERSION)
        return 0

    if "--help" in argv or "-h" in argv:
        print_help()
        return 0

    return run_bundled_client(argv)


if __name__ == "__main__":
    raise SystemExit(main())
