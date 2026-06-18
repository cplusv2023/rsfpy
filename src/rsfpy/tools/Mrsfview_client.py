#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
rsfview_client.py

Local receiver for remote RSF SVG plots.

Client mode:
    python3 rsfview_client.py
    python3 rsfview_client.py --cli
    python3 rsfview_client.py --cli --foreground

Sender mode on remote server:
    sfspike n1=10 | rsfgraph | python3 rsfview_client.py --send
    sfspike n1=10 | rsfgraph | RSFVIEW_TOKEN='token' python3 rsfview_client.py --send

Config file:
    ~/.rsfpy_config
"""

import os
import sys
import json
import time
import hmac
import shlex
import queue
import socket
import struct
import shutil
import hashlib
import secrets
import tempfile
import argparse
import threading
import subprocess
from pathlib import Path


MAGIC = b"RSFVIEW2"
CONFIG_PATH = Path.home() / ".rsfpy_config"
MAX_PAYLOAD = 1024 * 1024 * 1024  # 1 GB

try:
    import tkinter as tk
    from tkinter import ttk, messagebox
    HAVE_TK = True
except Exception:
    HAVE_TK = False


# ----------------------------------------------------------------------
# Generic helpers
# ----------------------------------------------------------------------

def now_string():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def stdout_log(msg):
    print(msg, file=sys.stderr, flush=True)


def default_config():
    return {
        "version": 1,
        "last_profile": "",
        "profiles": [],
    }


def load_config():
    if not CONFIG_PATH.exists():
        cfg = default_config()
        save_config(cfg)
        return cfg

    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            cfg = json.load(f)

        if not isinstance(cfg, dict):
            return default_config()

        cfg.setdefault("version", 1)
        cfg.setdefault("last_profile", "")
        cfg.setdefault("profiles", [])

        if not isinstance(cfg["profiles"], list):
            cfg["profiles"] = []

        return cfg

    except Exception:
        return default_config()


def save_config(cfg):
    try:
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2, ensure_ascii=False)

        try:
            if os.name != "nt":
                os.chmod(CONFIG_PATH, 0o600)
        except Exception:
            pass

        return True

    except Exception:
        return False


def find_profile(cfg, name):
    for p in cfg.get("profiles", []):
        if p.get("name") == name:
            return p
    return None


def upsert_profile(cfg, profile):
    name = profile.get("name") or "default"
    profiles = cfg.setdefault("profiles", [])

    for i, p in enumerate(profiles):
        if p.get("name") == name:
            profiles[i] = profile
            cfg["last_profile"] = name
            return

    profiles.append(profile)
    cfg["last_profile"] = name


def delete_profile(cfg, name):
    cfg["profiles"] = [p for p in cfg.get("profiles", []) if p.get("name") != name]
    if cfg.get("last_profile") == name:
        cfg["last_profile"] = cfg["profiles"][0].get("name", "") if cfg["profiles"] else ""


def detect_ssh():
    return shutil.which("ssh") or ""


def normalize_ssh_cmd(ssh_cmd):
    if not ssh_cmd:
        return "ssh"

    ssh_cmd = os.path.expanduser(os.path.expandvars(ssh_cmd))

    if os.path.isdir(ssh_cmd):
        exe = "ssh.exe" if os.name == "nt" else "ssh"
        return os.path.join(ssh_cmd, exe)

    return ssh_cmd


def split_command(cmd):
    if isinstance(cmd, list):
        return cmd

    if not cmd:
        return []

    return shlex.split(str(cmd), posix=(os.name != "nt"))


def quote_cmd(cmd):
    return " ".join(shlex.quote(str(x)) for x in cmd)


def token_hash(token, salt=None):
    if salt is None:
        salt = secrets.token_hex(16)

    data = (salt + "\n" + str(token)).encode("utf-8", errors="replace")
    return salt, hashlib.sha256(data).hexdigest()


def verify_token(profile, token):
    expected = profile.get("token_hash", "")
    salt = profile.get("token_salt", "")

    if not expected:
        return True

    _salt, got = token_hash(token or "", salt=salt)
    return hmac.compare_digest(got, expected)


def safe_name(s):
    s = str(s or "").strip()
    if not s:
        return "figure"

    bad = '<>:"/\\|?*\0'
    for ch in bad:
        s = s.replace(ch, "_")

    s = s.replace(" ", "_")
    return (s[:80] or "figure")


def recv_all(conn, n):
    chunks = []
    left = n

    while left > 0:
        data = conn.recv(min(1024 * 1024, left))
        if not data:
            raise RuntimeError("connection closed before receiving all data")
        chunks.append(data)
        left -= len(data)

    return b"".join(chunks)


def profile_summary(p):
    name = p.get("name", "unnamed")
    user = p.get("user", "")
    host = p.get("host", "")
    ssh_port = p.get("ssh_port", 22)
    remote_port = p.get("remote_port", 17890)
    local_port = p.get("local_port", 17890)
    backend = p.get("backend", "gtk")

    return "%s  [%s@%s:%s  R:%s -> L:%s  %s]" % (
        name, user, host, ssh_port, remote_port, local_port, backend
    )


def make_default_profile():
    user = os.environ.get("USER", "") or os.environ.get("USERNAME", "")
    return {
        "name": "default",
        "ssh_cmd": detect_ssh() or "ssh",
        "user": user,
        "host": "",
        "ssh_port": 22,
        "local_host": "127.0.0.1",
        "local_port": 17890,
        "remote_bind_host": "127.0.0.1",
        "remote_port": 17890,
        "viewer_cmd": "svgviewer",
        "backend": "gtk",
        "save_dir": "",
        "extra_ssh_args": "",
        "token_salt": "",
        "token_hash": "",
    }


def coerce_profile_values(p):
    out = make_default_profile()
    out.update(p or {})

    for key, default in (
        ("ssh_port", 22),
        ("local_port", 17890),
        ("remote_port", 17890),
    ):
        try:
            out[key] = int(out.get(key, default) or default)
        except Exception:
            out[key] = default

    out["name"] = str(out.get("name") or "default").strip() or "default"
    out["ssh_cmd"] = str(out.get("ssh_cmd") or "ssh").strip() or "ssh"
    out["local_host"] = str(out.get("local_host") or "127.0.0.1").strip() or "127.0.0.1"
    out["remote_bind_host"] = str(out.get("remote_bind_host") or "127.0.0.1").strip() or "127.0.0.1"
    out["backend"] = str(out.get("backend") or "gtk").strip() or "gtk"
    out["viewer_cmd"] = str(out.get("viewer_cmd") or "svgviewer").strip() or "svgviewer"

    return out


def remote_send_hint(profile, token=None, script_name="rsfview_client.py"):
    port = int(profile.get("remote_port", 17890))
    token_part = ""

    if token:
        token_part = "RSFVIEW_TOKEN=%s " % shlex.quote(token)
    elif profile.get("token_hash"):
        token_part = "RSFVIEW_TOKEN=<your-token> "

    return (
        "Remote usage example:\n"
        "    sfspike n1=10 | rsfgraph | "
        "%spython3 %s --send --port %d\n" % (token_part, script_name, port)
    )


# ----------------------------------------------------------------------
# Sender mode
# ----------------------------------------------------------------------

def send_payload(host, port, token, data, meta):
    token_bytes = (token or "").encode("utf-8", errors="replace")
    meta_bytes = json.dumps(meta or {}, ensure_ascii=False).encode("utf-8")

    if len(token_bytes) > 65535:
        raise RuntimeError("token is too long")

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
        description="Send SVG from stdin to rsfview client."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17890)
    parser.add_argument("--token", default=os.environ.get("RSFVIEW_TOKEN", ""))
    parser.add_argument("--title", default="")

    ns = parser.parse_args(argv)

    data = sys.stdin.buffer.read()
    if not data:
        print("[rsfview-send] empty stdin", file=sys.stderr)
        return 1

    meta = {
        "title": ns.title,
        "time": now_string(),
    }

    send_payload(ns.host, ns.port, ns.token, data, meta)
    print("[rsfview-send] sent %d bytes" % len(data), file=sys.stderr)
    return 0


# ----------------------------------------------------------------------
# Runtime
# ----------------------------------------------------------------------

class ClientRuntime:
    def __init__(self, profile, log_callback=None):
        self.profile = coerce_profile_values(profile)
        self.log_callback = log_callback or stdout_log

        self.stop_event = threading.Event()
        self.listener_thread = None
        self.tunnel_thread = None
        self.ssh_proc = None
        self.sock = None
        self.started = False

    def log(self, msg):
        try:
            self.log_callback("[%s] %s" % (now_string(), msg))
        except Exception:
            pass

    def start(self):
        if self.started:
            return True

        if not self.start_listener_socket():
            return False

        self.stop_event.clear()
        self.listener_thread = threading.Thread(
            target=self.listen_loop,
            name="rsfview-listener",
            daemon=True,
        )
        self.listener_thread.start()

        self.tunnel_thread = threading.Thread(
            target=self.tunnel_loop,
            name="rsfview-ssh-tunnel",
            daemon=True,
        )
        self.tunnel_thread.start()

        self.started = True
        return True

    def start_listener_socket(self):
        p = self.profile
        local_host = p.get("local_host", "127.0.0.1")
        local_port = int(p.get("local_port", 17890))

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((local_host, local_port))
            sock.listen(32)
            sock.settimeout(0.5)
            self.sock = sock
            self.log("listening on %s:%s" % (local_host, local_port))
            return True
        except Exception as e:
            self.log("failed to listen on %s:%s: %s" % (local_host, local_port, e))
            return False

    def stop(self):
        self.log("disconnect requested")
        self.stop_event.set()

        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass
        self.sock = None

        try:
            if self.ssh_proc and self.ssh_proc.poll() is None:
                self.ssh_proc.terminate()
                try:
                    self.ssh_proc.wait(timeout=3.0)
                except Exception:
                    self.ssh_proc.kill()
        except Exception:
            pass
        self.ssh_proc = None
        self.started = False

    def build_ssh_cmd(self):
        p = self.profile

        ssh_cmd = normalize_ssh_cmd(p.get("ssh_cmd", "ssh"))
        user = p.get("user", "")
        host = p.get("host", "")
        ssh_port = int(p.get("ssh_port", 22))

        local_host = p.get("local_host", "127.0.0.1")
        local_port = int(p.get("local_port", 17890))
        remote_bind_host = p.get("remote_bind_host", "127.0.0.1")
        remote_port = int(p.get("remote_port", 17890))

        target = "%s@%s" % (user, host) if user else host
        forward = "%s:%d:%s:%d" % (
            remote_bind_host,
            remote_port,
            local_host,
            local_port,
        )

        cmd = [
            ssh_cmd,
            "-N",
            "-p", str(ssh_port),
            "-o", "ExitOnForwardFailure=yes",
            "-o", "ServerAliveInterval=30",
            "-o", "ServerAliveCountMax=3",
            "-R", forward,
        ]

        extra = p.get("extra_ssh_args", "")
        if extra:
            cmd.extend(split_command(extra))

        cmd.append(target)
        return cmd

    def tunnel_loop(self):
        while not self.stop_event.is_set():
            cmd = self.build_ssh_cmd()
            self.log("starting ssh tunnel: %s" % quote_cmd(cmd))

            try:
                self.ssh_proc = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    stdin=subprocess.DEVNULL,
                )
            except Exception as e:
                self.log("failed to start ssh: %s" % e)
                time.sleep(3.0)
                continue

            reader = threading.Thread(
                target=self.read_ssh_output,
                args=(self.ssh_proc,),
                daemon=True,
            )
            reader.start()

            while not self.stop_event.is_set():
                ret = self.ssh_proc.poll()
                if ret is not None:
                    self.log("ssh tunnel exited with code %s" % ret)
                    break
                time.sleep(0.5)

            if self.stop_event.is_set():
                break

            self.log("ssh tunnel will restart in 3 seconds")
            time.sleep(3.0)

    def read_ssh_output(self, proc):
        try:
            if not proc.stdout:
                return
            for line in proc.stdout:
                line = line.rstrip("\n")
                if line:
                    self.log("ssh: " + line)
        except Exception:
            pass

    def listen_loop(self):
        sock = self.sock
        if sock is None:
            return

        while not self.stop_event.is_set():
            try:
                conn, addr = sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            except Exception as e:
                self.log("accept failed: %s" % e)
                continue

            self.log("incoming connection from %s" % (addr,))
            t = threading.Thread(
                target=self.safe_handle_connection,
                args=(conn,),
                daemon=True,
            )
            t.start()

        self.log("listener stopped")

    def safe_handle_connection(self, conn):
        try:
            self.handle_connection(conn)
        except Exception as e:
            self.log("connection error: %s" % e)
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def handle_connection(self, conn):
        conn.settimeout(30.0)

        magic = recv_all(conn, 8)
        if magic != MAGIC:
            raise RuntimeError("bad magic: %r" % (magic,))

        token_len = struct.unpack("!H", recv_all(conn, 2))[0]
        token = recv_all(conn, token_len).decode("utf-8", errors="replace")

        if not verify_token(self.profile, token):
            raise RuntimeError("bad token")

        meta_len = struct.unpack("!I", recv_all(conn, 4))[0]
        if meta_len > 16 * 1024 * 1024:
            raise RuntimeError("metadata too large")

        meta_bytes = recv_all(conn, meta_len)
        try:
            meta = json.loads(meta_bytes.decode("utf-8", errors="replace"))
        except Exception:
            meta = {}

        size = struct.unpack("!Q", recv_all(conn, 8))[0]
        if size <= 0:
            raise RuntimeError("empty SVG payload")
        if size > MAX_PAYLOAD:
            raise RuntimeError("payload too large: %d" % size)

        data = recv_all(conn, size)
        self.log("received SVG payload: %d bytes" % len(data))

        path = self.write_svg(data, meta)
        self.open_viewer(path)

    def write_svg(self, data, meta):
        p = self.profile
        title = safe_name(meta.get("title") or "figure")

        save_dir = p.get("save_dir", "")
        if save_dir:
            base = Path(os.path.expanduser(os.path.expandvars(save_dir)))
            try:
                base.mkdir(parents=True, exist_ok=True)
            except Exception:
                base = Path(tempfile.gettempdir())
        else:
            base = Path(tempfile.gettempdir())

        filename = "%s_%s.svg" % (title, time.strftime("%Y%m%d_%H%M%S"))
        path = base / filename

        with open(path, "wb") as f:
            f.write(data)

        self.log("saved SVG: %s" % path)
        return str(path)

    def open_viewer(self, path):
        p = self.profile
        cmd = split_command(p.get("viewer_cmd", "svgviewer"))
        if not cmd:
            cmd = ["svgviewer"]

        backend = p.get("backend", "gtk")
        if backend and backend != "none":
            cmd.extend(["--backend", backend])

        cmd.append(path)
        self.log("open viewer: %s" % quote_cmd(cmd))

        try:
            subprocess.Popen(cmd)
        except Exception as e:
            self.log("failed to open viewer: %s" % e)


# ----------------------------------------------------------------------
# CLI profile editor
# ----------------------------------------------------------------------

def cli_ask(prompt, default=""):
    if default not in (None, ""):
        s = input("%s [%s]: " % (prompt, default)).strip()
        return s if s else str(default)
    return input("%s: " % prompt).strip()


def profile_from_cli(existing=None):
    old = coerce_profile_values(existing or {})
    is_edit = existing is not None

    print("\n%s profile:" % ("Edit" if is_edit else "New"))

    profile = dict(old)
    profile["name"] = cli_ask("Profile name", old.get("name", "default")) or "default"
    profile["ssh_cmd"] = cli_ask("SSH command path or directory", old.get("ssh_cmd") or detect_ssh() or "ssh") or "ssh"
    profile["user"] = cli_ask("SSH username", old.get("user", ""))
    profile["host"] = cli_ask("SSH host", old.get("host", ""))
    profile["ssh_port"] = int(cli_ask("SSH port", old.get("ssh_port", 22)) or 22)
    profile["local_host"] = cli_ask("Local listen host", old.get("local_host", "127.0.0.1")) or "127.0.0.1"
    profile["local_port"] = int(cli_ask("Local listen port", old.get("local_port", 17890)) or 17890)
    profile["remote_bind_host"] = cli_ask("Remote bind host", old.get("remote_bind_host", "127.0.0.1")) or "127.0.0.1"
    profile["remote_port"] = int(cli_ask("Remote reverse port", old.get("remote_port", 17890)) or 17890)
    profile["viewer_cmd"] = cli_ask("Local svgviewer command", old.get("viewer_cmd", "svgviewer")) or "svgviewer"
    profile["backend"] = cli_ask("Viewer backend: gtk/x11/auto/none", old.get("backend", "gtk")) or "gtk"
    profile["save_dir"] = cli_ask("Save received SVG dir, empty means system temp", old.get("save_dir", ""))
    profile["extra_ssh_args"] = cli_ask("Extra ssh args, optional", old.get("extra_ssh_args", ""))

    token_prompt = "Transfer password/token, empty keeps current" if is_edit else "Transfer password/token, optional"
    token = cli_ask(token_prompt, "")
    if token:
        profile["token_salt"], profile["token_hash"] = token_hash(token)
        print("Transfer token is set. Plaintext token is not saved.", file=sys.stderr)
    elif not is_edit:
        profile["token_salt"] = ""
        profile["token_hash"] = ""

    return coerce_profile_values(profile), token


def choose_or_create_profile_cli(cfg, selected_name=None):
    if selected_name:
        p = find_profile(cfg, selected_name)
        if p:
            return p
        print("Profile not found: %s" % selected_name, file=sys.stderr)

    while True:
        profiles = cfg.get("profiles", [])
        if profiles:
            print("\nExisting profiles:")
            for i, p in enumerate(profiles, 1):
                print("  %d) %s" % (i, profile_summary(p)))
            print("  n) New profile")
            print("  e) Edit profile")
            print("  q) Quit")

            choice = input("Select profile [1]: ").strip()
            if not choice:
                return profiles[0]

            if choice.lower() == "q":
                return None

            if choice.lower() == "n":
                profile, token = profile_from_cli(None)
                upsert_profile(cfg, profile)
                save_config(cfg)
                print(remote_send_hint(profile, token), file=sys.stderr)
                return profile

            if choice.lower() == "e":
                idx_s = input("Profile number to edit: ").strip()
                try:
                    idx = int(idx_s) - 1
                    if 0 <= idx < len(profiles):
                        profile, token = profile_from_cli(profiles[idx])
                        upsert_profile(cfg, profile)
                        save_config(cfg)
                        print(remote_send_hint(profile, token), file=sys.stderr)
                        continue
                except Exception:
                    pass
                print("Invalid profile number.", file=sys.stderr)
                continue

            try:
                idx = int(choice) - 1
                if 0 <= idx < len(profiles):
                    return profiles[idx]
            except Exception:
                pass

            print("Invalid choice.", file=sys.stderr)
            continue

        print("\nNo existing profile. Create new profile:")
        profile, token = profile_from_cli(None)
        upsert_profile(cfg, profile)
        save_config(cfg)
        print(remote_send_hint(profile, token), file=sys.stderr)
        return profile


def background_command_args(profile_name):
    if getattr(sys, "frozen", False):
        cmd = [sys.executable]
    else:
        cmd = [sys.executable, os.path.abspath(__file__)]
    cmd.extend(["--cli", "--profile", profile_name, "--daemon-child"])
    return cmd


def spawn_background(profile_name):
    cmd = background_command_args(profile_name)

    kwargs = {
        "stdout": subprocess.DEVNULL,
        "stderr": subprocess.DEVNULL,
        "stdin": subprocess.DEVNULL,
        "close_fds": True,
    }

    if os.name == "nt":
        kwargs["creationflags"] = (
            getattr(subprocess, "DETACHED_PROCESS", 0)
            | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        )
    else:
        kwargs["start_new_session"] = True

    proc = subprocess.Popen(cmd, **kwargs)
    print("rsfview-client started in background, pid=%s" % proc.pid)
    return proc.pid


def run_runtime_forever(profile):
    rt = ClientRuntime(profile)
    if not rt.start():
        return 1

    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        rt.stop()

    return 0


def cli_main(args):
    cfg = load_config()
    profile = choose_or_create_profile_cli(cfg, selected_name=args.profile)

    if profile is None:
        return 0

    if args.daemon_child or args.foreground:
        return run_runtime_forever(profile)

    spawn_background(profile.get("name"))
    return 0


# ----------------------------------------------------------------------
# GUI
# ----------------------------------------------------------------------

class TkClientApp:
    FIELDS = [
        ("name", "Profile name", "default"),
        ("ssh_cmd", "SSH command path or directory", ""),
        ("user", "SSH username", ""),
        ("host", "SSH host", ""),
        ("ssh_port", "SSH port", "22"),
        ("local_host", "Local listen host", "127.0.0.1"),
        ("local_port", "Local listen port", "17890"),
        ("remote_bind_host", "Remote bind host", "127.0.0.1"),
        ("remote_port", "Remote reverse port", "17890"),
        ("viewer_cmd", "Local svgviewer command", "svgviewer"),
        ("backend", "Viewer backend", "gtk"),
        ("save_dir", "Save SVG dir, empty = system temp", ""),
        ("extra_ssh_args", "Extra ssh args", ""),
        ("token", "Transfer password/token", ""),
    ]

    def __init__(self, root):
        self.root = root
        self.root.title("RSF View Client")
        self.root.geometry("820x560")

        self.cfg = load_config()
        self.runtime = None
        self.log_queue = queue.Queue()
        self.form_entries = {}
        self.form_existing = None

        self.main_frame = ttk.Frame(root, padding=10)
        self.main_frame.pack(fill="both", expand=True)

        self.popup = tk.Menu(root, tearoff=0)
        self.popup.add_command(label="Disconnect", command=self.disconnect)
        self.popup.add_command(label="Exit", command=self.exit_app)
        self.root.bind("<Button-2>", self.show_popup)
        self.root.bind("<Button-3>", self.show_popup)

        self.show_start_screen()
        self.root.after(100, self.flush_log_queue)

    def show_popup(self, event):
        try:
            self.popup.tk_popup(event.x_root, event.y_root)
        finally:
            self.popup.grab_release()

    def clear_main(self):
        for child in self.main_frame.winfo_children():
            child.destroy()

    def show_start_screen(self):
        self.clear_main()
        self.root.deiconify()

        ttk.Label(
            self.main_frame,
            text="Select an SSH profile or create/edit one.",
            font=("TkDefaultFont", 11, "bold"),
        ).pack(anchor="w")

        self.profile_list = tk.Listbox(self.main_frame, height=10)
        self.profile_list.pack(fill="both", expand=True, pady=8)
        self.reload_profile_list()

        btns = ttk.Frame(self.main_frame)
        btns.pack(fill="x", pady=6)
        ttk.Button(btns, text="Connect", command=self.connect_selected).pack(side="left")
        ttk.Button(btns, text="New Profile", command=lambda: self.show_profile_form(None)).pack(side="left", padx=6)
        ttk.Button(btns, text="Edit Profile", command=self.edit_selected).pack(side="left")
        ttk.Button(btns, text="Delete Profile", command=self.delete_selected).pack(side="left", padx=6)
        ttk.Button(btns, text="Exit", command=self.exit_app).pack(side="right")

    def reload_profile_list(self):
        self.profile_list.delete(0, "end")
        profiles = self.cfg.get("profiles", [])
        for p in profiles:
            self.profile_list.insert("end", profile_summary(p))

        if profiles:
            last = self.cfg.get("last_profile", "")
            index = 0
            for i, p in enumerate(profiles):
                if p.get("name") == last:
                    index = i
                    break
            self.profile_list.selection_set(index)
            self.profile_list.see(index)

    def selected_profile_index(self):
        sel = self.profile_list.curselection()
        if not sel:
            return None
        return int(sel[0])

    def selected_profile(self):
        idx = self.selected_profile_index()
        profiles = self.cfg.get("profiles", [])
        if idx is None or idx < 0 or idx >= len(profiles):
            return None
        return profiles[idx]

    def connect_selected(self):
        p = self.selected_profile()
        if not p:
            messagebox.showwarning("RSF View Client", "Please select a profile.")
            return
        self.connect_profile(p)

    def edit_selected(self):
        p = self.selected_profile()
        if not p:
            messagebox.showwarning("RSF View Client", "Please select a profile to edit.")
            return
        self.show_profile_form(p)

    def delete_selected(self):
        p = self.selected_profile()
        if not p:
            messagebox.showwarning("RSF View Client", "Please select a profile to delete.")
            return
        if not messagebox.askyesno("Delete Profile", "Delete profile '%s'?" % p.get("name", "")):
            return
        delete_profile(self.cfg, p.get("name"))
        save_config(self.cfg)
        self.show_start_screen()

    def show_profile_form(self, existing=None):
        self.clear_main()
        self.form_entries = {}
        self.form_existing = existing

        title = "Edit Profile" if existing else "New Profile"
        ttk.Label(self.main_frame, text=title, font=("TkDefaultFont", 11, "bold")).pack(anchor="w")

        form_wrap = ttk.Frame(self.main_frame)
        form_wrap.pack(fill="both", expand=True, pady=8)

        profile = coerce_profile_values(existing or {})
        if not existing and not profile.get("ssh_cmd"):
            profile["ssh_cmd"] = detect_ssh() or "ssh"

        for row, (key, label, default) in enumerate(self.FIELDS):
            ttk.Label(form_wrap, text=label + ":").grid(row=row, column=0, sticky="e", padx=5, pady=3)

            if key == "backend":
                var = tk.StringVar(value=str(profile.get(key, default) or default))
                widget = ttk.Combobox(form_wrap, textvariable=var, values=["gtk", "x11", "auto", "none"], width=36)
                widget.grid(row=row, column=1, sticky="we", padx=5, pady=3)
                self.form_entries[key] = var
            elif key == "token":
                var = tk.StringVar(value="")
                widget = ttk.Entry(form_wrap, textvariable=var, width=40, show="*")
                widget.grid(row=row, column=1, sticky="we", padx=5, pady=3)
                hint = "leave empty to keep current token" if existing else "optional"
                ttk.Label(form_wrap, text=hint).grid(row=row, column=2, sticky="w", padx=5, pady=3)
                self.form_entries[key] = var
            else:
                value = profile.get(key, default)
                if key == "ssh_cmd" and not value:
                    value = detect_ssh() or "ssh"
                var = tk.StringVar(value=str(value if value is not None else default))
                widget = ttk.Entry(form_wrap, textvariable=var, width=40)
                widget.grid(row=row, column=1, sticky="we", padx=5, pady=3)
                self.form_entries[key] = var

        form_wrap.columnconfigure(1, weight=1)

        btns = ttk.Frame(self.main_frame)
        btns.pack(fill="x", pady=6)
        ttk.Button(btns, text="Save", command=lambda: self.save_profile(connect=False)).pack(side="left")
        ttk.Button(btns, text="Save && Connect", command=lambda: self.save_profile(connect=True)).pack(side="left", padx=6)
        ttk.Button(btns, text="Cancel", command=self.show_start_screen).pack(side="right")

    def collect_form_profile(self):
        old = coerce_profile_values(self.form_existing or {})
        profile = dict(old)
        token = ""

        for key, _label, _default in self.FIELDS:
            value = self.form_entries[key].get().strip()
            if key == "token":
                token = value
            else:
                profile[key] = value

        for key in ("ssh_port", "local_port", "remote_port"):
            try:
                profile[key] = int(profile.get(key, ""))
            except Exception:
                raise ValueError("%s must be an integer" % key)

        if not profile.get("name"):
            raise ValueError("profile name is empty")
        if not profile.get("host"):
            raise ValueError("SSH host is empty")
        if not profile.get("ssh_cmd"):
            raise ValueError("SSH command is empty")

        if token:
            profile["token_salt"], profile["token_hash"] = token_hash(token)
        elif not self.form_existing:
            profile["token_salt"] = ""
            profile["token_hash"] = ""

        return coerce_profile_values(profile), token

    def save_profile(self, connect=False):
        try:
            profile, token = self.collect_form_profile()
        except Exception as e:
            messagebox.showerror("Invalid Profile", str(e))
            return

        upsert_profile(self.cfg, profile)
        saved = save_config(self.cfg)

        msg = remote_send_hint(profile, token)
        if not saved:
            msg = "Warning: failed to save config.\n\n" + msg

        messagebox.showinfo("Profile Saved", msg)

        if connect:
            self.connect_profile(profile)
        else:
            self.show_start_screen()

    def show_run_screen(self):
        self.clear_main()

        btns = ttk.Frame(self.main_frame)
        btns.pack(fill="x")
        ttk.Button(btns, text="Disconnect", command=self.disconnect).pack(side="left")
        ttk.Button(btns, text="Exit", command=self.exit_app).pack(side="left", padx=6)

        self.text = tk.Text(self.main_frame, height=18, wrap="word")
        self.text.pack(fill="both", expand=True, pady=8)
        self.text.bind("<Button-2>", self.show_popup)
        self.text.bind("<Button-3>", self.show_popup)

    def connect_profile(self, profile):
        self.show_run_screen()
        self.enqueue_log("using profile: %s" % profile_summary(profile))

        self.runtime = ClientRuntime(profile, log_callback=self.enqueue_log)
        if not self.runtime.start():
            self.enqueue_log("connection failed before SSH tunnel start")
            messagebox.showerror(
                "Connection Failed",
                "Failed to start listener. The local port may already be in use."
            )
            self.runtime = None
            self.show_start_screen()
            return

        self.root.after(800, self.root.iconify)

    def disconnect(self):
        if self.runtime:
            self.runtime.stop()
            self.runtime = None
        self.show_start_screen()

    def exit_app(self):
        try:
            if self.runtime:
                self.runtime.stop()
        finally:
            self.root.destroy()

    def enqueue_log(self, msg):
        self.log_queue.put(str(msg))

    def flush_log_queue(self):
        try:
            while True:
                msg = self.log_queue.get_nowait()
                if hasattr(self, "text") and self.text.winfo_exists():
                    self.text.insert("end", msg + "\n")
                    self.text.see("end")
        except queue.Empty:
            pass
        self.root.after(100, self.flush_log_queue)


def gui_main():
    if not HAVE_TK:
        return None
    root = tk.Tk()
    app = TkClientApp(root)
    root.protocol("WM_DELETE_WINDOW", app.exit_app)
    root.mainloop()
    return 0


# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------

def build_parser():
    parser = argparse.ArgumentParser(
        description="RSF view client: SSH reverse tunnel + local SVG receiver."
    )
    parser.add_argument("--send", action="store_true", help="send stdin SVG to client")
    parser.add_argument("--cli", action="store_true", help="force CLI mode")
    parser.add_argument("--gui", action="store_true", help="force GUI mode")
    parser.add_argument("--profile", default="", help="profile name")
    parser.add_argument("--foreground", action="store_true", help="CLI: do not daemonize")
    parser.add_argument("--daemon-child", action="store_true", help=argparse.SUPPRESS)
    return parser


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv

    if "--send" in argv:
        idx = argv.index("--send")
        return sender_main(argv[:idx] + argv[idx + 1:])

    parser = build_parser()
    args = parser.parse_args(argv)

    if args.cli:
        return cli_main(args)

    if args.gui:
        if HAVE_TK:
            return gui_main()
        print("tkinter is not available; falling back to CLI.", file=sys.stderr)
        return cli_main(args)

    if HAVE_TK:
        return gui_main()

    return cli_main(args)


if __name__ == "__main__":
    raise SystemExit(main())
