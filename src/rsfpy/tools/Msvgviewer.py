#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__doc__ = """
\033[1mNAME\033[0m
    \tMsvgviewer.py
\033[1mDESCRIPTION\033[0m
    \tView SVG images.

    \tIn SSH mode, backend=auto first tries:
    \t    rsfclient --send
    \tIf sending fails, it tries the X11 backend.

\033[1mSYNOPSIS\033[0m
    \tMsvgviewer.py [--backend client|gtk|x11|auto] [< in0.svg] in1.svg in2.svg ...

\033[1mBACKENDS\033[0m
    \tauto
    \t    Local mode: GTK.
    \t    SSH mode: client -> X11.

    \tclient
    \t    Send SVG data to rsfclient --send.
    \t    This is intended for SSH sessions with a local rsfclient receiver.

    \tgtk
    \t    Open with local svgviewer-gtk.

    \tx11
    \t    Open with local svgviewer-x11.

\033[1mENVIRONMENT\033[0m
    \tRSFPY_SVGVIEWER_BACKEND
    \t    Default backend: auto, client, gtk, or x11.

    \tRSFPY_SVGVIEWER_REMOTE
    \t    Force remote detection. Values: 1/0, true/false, yes/no, on/off.

    \tRSFPY_SVGVIEWER_CLIENT_CMD
    \t    Client sender command. Default: rsfclient.

\033[1mMORE INFO\033[0m
    \tAuthor:\tauthor_label
    \tEmail:\temail_label
    \tSource:\tgithub_label
\033[1mVERSION\033[0m
    \tversion_label
"""

import os
import sys
import shlex
import shutil
import argparse
import getpass
import tempfile
from subprocess import run
from textwrap import dedent
import importlib.resources

from rsfpy import bin
from rsfpy.utils import _get_stdname, _str_match_re
from rsfpy.version import __version__, __email__, __author__, __github__


__progname__ = os.path.basename(sys.argv[0])

__doc__ = __doc__.replace("author_label", __author__)
__doc__ = __doc__.replace("email_label", __email__)
__doc__ = __doc__.replace("github_label", __github__)
__doc__ = __doc__.replace("version_label", __version__)

DOC = dedent(__doc__.replace("Msvgviewer.py", __progname__))


def resource_path(name):
    return importlib.resources.files(bin).joinpath(name)


svgviewer_path_gtk = resource_path("svgviewer-gtk")
svgviewer_path_x11 = resource_path("svgviewer-x11")

VALID_BACKENDS = ("auto", "client", "gtk", "x11")


def is_executable(path):
    try:
        return path.is_file() and os.access(str(path), os.X_OK)
    except Exception:
        return False


def env_truthy(value):
    return str(value).strip().lower() in ("1", "true", "yes", "on", "y")


def env_falsey(value):
    return str(value).strip().lower() in ("0", "false", "no", "off", "n")


def is_remote_mode():
    """
    Detect SSH mode.

    Priority:
        1. RSFPY_SVGVIEWER_REMOTE explicitly controls behavior.
        2. SSH_CONNECTION / SSH_CLIENT / SSH_TTY indicates a SSH session.

    DISPLAY is not required here, because backend=client is useful even
    without X forwarding.
    """
    override = os.environ.get("RSFPY_SVGVIEWER_REMOTE")

    if override is not None:
        if env_truthy(override):
            return True
        if env_falsey(override):
            return False

    return any(
        os.environ.get(k)
        for k in ("SSH_CONNECTION", "SSH_CLIENT", "SSH_TTY")
    )


def build_parser():
    parser = argparse.ArgumentParser(
        prog=__progname__,
        add_help=False,
        description="View SVG images.",
    )

    parser.add_argument(
        "--backend",
        choices=["auto", "client", "gtk", "x11"],
        default=os.environ.get("RSFPY_SVGVIEWER_BACKEND", "auto"),
        help="Select SVG viewer backend: auto, client, gtk, or x11. "
             "Default: auto. Can also be set by RSFPY_SVGVIEWER_BACKEND.",
    )

    parser.add_argument(
        "-h", "--help",
        action="store_true",
        help="Show help message.",
    )

    parser.add_argument(
        "files",
        nargs="*",
        help="SVG files.",
    )

    return parser


def normalize_backend(value):
    value = (value or "").strip().lower()
    if value in VALID_BACKENDS:
        return value
    return None


def parse_common_cli(argv=None):
    """
    Parse wrapper-level options permissively.

    Madagascar programs commonly use key=value arguments.  These wrappers only
    consume help and backend controls here; the caller decides what remaining
    key=value pairs mean.  Unknown --options are ignored so they do not become
    accidental filenames.
    """
    if argv is None:
        argv = sys.argv[1:]

    backend = normalize_backend(os.environ.get("RSFPY_SVGVIEWER_BACKEND")) or "auto"
    items = []
    help_requested = False
    i = 0

    while i < len(argv):
        item = argv[i]

        if item in ("-h", "--help"):
            help_requested = True
            i += 1
            continue

        if item == "--backend":
            if i + 1 < len(argv):
                chosen = normalize_backend(argv[i + 1])
                if chosen:
                    backend = chosen
                i += 2
            else:
                i += 1
            continue

        if item.startswith("--backend="):
            chosen = normalize_backend(item.split("=", 1)[1])
            if chosen:
                backend = chosen
            i += 1
            continue

        if item == "--watch":
            items.append(item)
            if i + 1 < len(argv):
                items.append(argv[i + 1])
                i += 2
            else:
                i += 1
            continue

        if item.startswith("--watch="):
            items.append(item)
            i += 1
            continue

        if "=" in item:
            parsed = _str_match_re([item])
            key, value = next(iter(parsed.items()), ("", ""))
            if key == "backend":
                chosen = normalize_backend(value)
                if chosen:
                    backend = chosen
            else:
                items.append(item)
            i += 1
            continue

        if item.startswith("--"):
            if (
                "=" not in item
                and i + 1 < len(argv)
                and not argv[i + 1].startswith("--")
                and "=" not in argv[i + 1]
            ):
                i += 2
            else:
                i += 1
            continue

        items.append(item)
        i += 1

    return backend, items, help_requested


def split_svg_args(items):
    files = []
    keep_next = False
    for item in items:
        if keep_next:
            files.append(item)
            keep_next = False
            continue

        if item == "-":
            files.append(item)
            continue
        if item == "--watch":
            files.append(item)
            keep_next = True
            continue
        if item.startswith("--watch="):
            files.append(item)
            continue
        if "=" in item:
            continue
        if item.startswith("--"):
            continue
        files.append(item)
    return files


def readable_input_args(args):
    files = []
    keep_next = False
    for item in args:
        if keep_next:
            files.append(item)
            keep_next = False
            continue

        if item == "-":
            files.append(item)
            continue
        if item == "--watch":
            files.append(item)
            keep_next = True
            continue
        if item.startswith("--watch="):
            files.append(item)
            continue
        try:
            with open(item, "rb"):
                pass
        except OSError:
            continue
        files.append(item)
    return files


def get_backend_order(backend):
    """
    Return backend order.

    Explicit backend selection is respected.

    Auto mode:
        local  -> GTK
        SSH    -> client -> X11
    """
    if backend == "client":
        return [("CLIENT", None)]

    if backend == "gtk":
        return [("GTK", svgviewer_path_gtk)]

    if backend == "x11":
        return [("X11", svgviewer_path_x11)]

    if is_remote_mode():
        order = [("CLIENT", None)]
        if os.environ.get("DISPLAY"):
            order.append(("X11", svgviewer_path_x11))
        return order

    return [
        ("GTK", svgviewer_path_gtk),
    ]


def viewer_env_for_backend(name):
    """
    Return environment for subprocess.

    For GTK, default to GSK_RENDERER=cairo unless user already set it.
    This helps on X forwarding, WSL, XQuartz, and systems with broken EGL/GLX.
    """
    env = os.environ.copy()

    if name == "GTK":
        env.setdefault("GSK_RENDERER", "cairo")

        if is_remote_mode():
            env.setdefault("GDK_BACKEND", "x11")
            env.setdefault("RSFPY_SVGVIEWER_REMOTE", "1")

    return env


def split_cmd(cmd):
    if not cmd:
        return []

    if isinstance(cmd, (list, tuple)):
        return list(cmd)

    return shlex.split(str(cmd), posix=(os.name != "nt"))


def client_sender_command():
    """
    Return command used to send SVG data to the local receiver.

    Default:
        rsfclient --send

    Override:
        RSFPY_SVGVIEWER_CLIENT_CMD="python3 /path/to/rsfview_client.py"
        RSFPY_SVGVIEWER_CLIENT_CMD="rsfclient"
    """
    cmd = os.environ.get("RSFPY_SVGVIEWER_CLIENT_CMD", "rsfclient")
    parts = split_cmd(cmd)

    if not parts:
        parts = ["rsfclient"]

    parts.append("--send")
    return parts


def current_server_user():
    for key in ("RSFCLIENT_SERVER_USER", "USER", "LOGNAME", "USERNAME"):
        value = os.environ.get(key)
        if value:
            return value

    try:
        return getpass.getuser()
    except Exception:
        return "unknown"


def pair_config_paths():
    user = current_server_user()
    name = ".rsfclient_server_%s" % user
    paths = []

    for base in (tempfile.gettempdir(), os.path.expanduser("~")):
        if base and base not in ("~", ""):
            paths.append(os.path.join(base, name))

    return paths


def read_pair_config():
    for path in pair_config_paths():
        try:
            with open(path, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except OSError:
            continue

        data = {}
        for line in lines:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            data[key.strip()] = value.strip()

        if data:
            return data, path

    return {}, None


def client_connection_options():
    cfg, cfg_path = read_pair_config()
    host = (
        cfg.get("host")
        or os.environ.get("RSFCLIENT_SEND_HOST")
        or os.environ.get("RSFVIEW_HOST")
        or "127.0.0.1"
    )
    port = (
        cfg.get("port")
        or os.environ.get("RSFCLIENT_SEND_PORT")
        or os.environ.get("RSFVIEW_PORT")
        or "17890"
    )
    token = (
        cfg.get("token")
        or os.environ.get("RSFCLIENT_SEND_TOKEN")
        or os.environ.get("RSFVIEW_TOKEN")
        or ""
    )
    return host, port, token, cfg_path


def remove_stale_pair_config(path):
    if not path:
        return

    try:
        os.unlink(path)
        sys.stderr.write("Removed stale rsfclient pair config: %s\n" % path)
    except OSError:
        pass


def command_exists(cmd):
    if not cmd:
        return False

    exe = cmd[0]

    if os.path.isabs(exe) or os.path.sep in exe:
        return os.path.exists(exe)

    return shutil.which(exe) is not None


def read_svg_sources(args, stdin_data):
    """
    Read SVG inputs from file arguments and stdin.

    Rules:
        - file arguments are read in command-line order.
        - '-' means stdin position.
        - if stdin exists but '-' is not used, stdin is appended after files.

    Note:
        Shell redirection does not preserve its textual position. For example:
            svgviewer a1.svg < a2.svg a3.svg
        gives this program argv=[a1.svg, a3.svg] and stdin=a2.svg.
        If exact ordering is needed, use:
            svgviewer a1.svg - a3.svg < a2.svg
    """
    sources = []
    stdin_used = False
    skip_next = False

    for item in args:
        if skip_next:
            skip_next = False
            continue

        if item == "--watch":
            skip_next = True
            continue

        if item.startswith("--watch="):
            continue

        if item == "-":
            if stdin_data is None:
                raise RuntimeError("'-' was specified but stdin is empty")
            sources.append(("stdin", stdin_data, "stdin"))
            stdin_used = True
            continue

        try:
            with open(item, "rb") as f:
                data = f.read()
        except OSError:
            continue

        title = os.path.splitext(os.path.basename(item))[0] or "figure"
        sources.append((title, data, item))

    if stdin_data is not None and not stdin_used:
        sources.append(("stdin", stdin_data, "stdin"))

    return sources


def xml_attr_escape(value):
    value = "" if value is None else str(value)
    return (
        value.replace("&", "&amp;")
        .replace('"', "&quot;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace("\n", " ")
        .replace("\r", " ")
        .replace("\t", " ")
    )


def frame_marker(title, source_path):
    filename = source_path if source_path and source_path != "stdin" else "stdin"
    label_base = os.path.basename(filename) if filename != "stdin" else filename
    label = title or os.path.splitext(label_base)[0] or "figure"
    return (
        '<!-- RSFPY_SPLIT filename="%s" framelabel="%s" -->\n'
        % (xml_attr_escape(filename), xml_attr_escape(label))
    ).encode("utf-8")


def build_svg_sequence_payload(args, stdin_data):
    """
    Build one payload for client backend.

    A single SVG is sent unchanged.
    Multiple SVGs are joined by __SVG_SPLITTER so that local svgviewer
    opens them as one sequence/window.
    """
    sources = read_svg_sources(args, stdin_data)

    if not sources:
        raise RuntimeError("no SVG input for client backend")

    chunks = []
    for title, data, source_path in sources:
        chunks.append(frame_marker(title, source_path))
        chunks.append(data)
        chunks.append(b"\n")

    if len(sources) == 1:
        return sources[0][0], b"".join(chunks)

    title = "sequence_%d" % len(sources)
    return title, b"".join(chunks)


def send_with_client(args, stdin_data):
    """
    Send SVG data through rsfclient --send.

    All SVG inputs are packed into one payload:
        - one input  -> raw SVG
        - many inputs -> RSFPY_SPLIT SVG sequence

    Return subprocess-style return code.
    """
    cmd_base = client_sender_command()

    if not command_exists(cmd_base):
        sys.stderr.write(
            "Client sender not found: %s\n"
            % " ".join(shlex.quote(x) for x in cmd_base)
        )
        return 127

    try:
        title, payload = build_svg_sequence_payload(args, stdin_data)
        host, port, token, cfg_path = client_connection_options()

        cmd = list(cmd_base)
        cmd += ["--host", host, "--port", str(port)]
        if token:
            cmd += ["--token", token]
        if title:
            cmd += ["--title", title]

        if cfg_path:
            sys.stderr.write("Using rsfclient pair config: %s\n" % cfg_path)

        result = run(
            cmd,
            input=payload,
            capture_output=False,
        )

        if result.returncode != 0 and cfg_path:
            remove_stale_pair_config(cfg_path)

        return result.returncode

    except Exception as e:
        sys.stderr.write("Client sender failed: %s\n" % e)
        return 1



def run_viewer(backend, args, stdin_data):
    tried = []

    for name, path in get_backend_order(backend):
        if name == "CLIENT":
            ret = send_with_client(args, stdin_data)

            if ret == 0:
                return 0

            tried.append("CLIENT: exited with code %s: %s" % (
                ret,
                " ".join(shlex.quote(x) for x in client_sender_command()),
            ))

            # 用户显式指定 client 时，不自动 fallback
            if backend == "client":
                return ret

            # auto 模式下 client 失败，继续尝试 X11 后端
            continue

        if not is_executable(path):
            tried.append(f"{name}: not found or not executable: {path}")
            continue

        result = run(
            [str(path)] + args,
            input=stdin_data,
            capture_output=False,
            env=viewer_env_for_backend(name),
        )

        if result.returncode == 0:
            return 0

        tried.append(f"{name}: exited with code {result.returncode}: {path}")

        return result.returncode

    sys.stderr.write("No usable SVG viewer backend succeeded.\n")
    for item in tried:
        sys.stderr.write(f"  {item}\n")

    return 1


def show_help(exit_code=0):
    try:
        run(["less", "-R"], input=DOC.encode())
    except Exception:
        sys.stdout.write(DOC)
    sys.exit(exit_code)


def main():
    backend, raw_args, help_requested = parse_common_cli()

    if help_requested:
        show_help(0)

    args = split_svg_args(raw_args)
    stdin_data = None

    # in case pseudo shell
    stdname = _get_stdname()
    if stdname[1] and os.path.exists(stdname[1]) and stdname[1].endswith(".svg"):
        args = [stdname[1]] + args
    elif not sys.stdin.isatty():
        stdin_data = sys.stdin.buffer.read()

    args = readable_input_args(args)
    ret = run_viewer(backend, args, stdin_data)
    sys.exit(ret)


if __name__ == "__main__":
    main()
