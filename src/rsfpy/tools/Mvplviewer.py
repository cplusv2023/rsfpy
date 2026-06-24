#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__doc__ = """
\033[1mNAME\033[0m
    \tMvplviewer.py
\033[1mDESCRIPTION\033[0m
    \tView VPL images by converting them to SVG first.

    \tThe original VPL filename is kept in the SVG sequence metadata so the
    \tviewer still reports the source VPL file, not a temporary SVG file.

\033[1mSYNOPSIS\033[0m
    \tMvplviewer.py [--backend client|gtk|x11|auto] [conversion options] [< in0.vpl] in1.vpl ...

\033[1mCONVERSION OPTIONS\033[0m
    \tbgcolor=white, --bgcolor white
    \t    Fill the converted SVG canvas. Accepts matplotlib-like short names
    \t    (r, b, k), common names (red, blue), and #rgb/#rgba/#rrggbb/#rrggbbaa.

    \tfont=, fontfamily=, fontsz=, fontcolor=, fontfat=/fontwidth=/fontweight=
    \t    Global text defaults. More specific title*/label*/barlabel*/
    \t    scalebar* options override these values.

    \tframecolor=, framefat=/framewidth=
    \t    Global frame/axis/grid defaults.

    \taxiscolor=, gridcolor=, labelcolor=, titlecolor=
    \t    Group-specific overrides. color/col accept named colors, #hex,
    \t    grayscale floats, or VPL palette indexes.

\033[1mENVIRONMENT\033[0m
    \tRSFVPLOPTS
    \t    Default VPL conversion options, for example:
    \t    RSFVPLOPTS='bgcolor=k fontsz=15 axiscolor=#cccccc'

    \tRSFPY_VPLVIEWER_CONVERTER
    \t    VPL-to-SVG converter command. Default: bundled vpl2svg.

    \tRSFPY_SVGVIEWER_BACKEND
    \t    Default backend: auto, client, gtk, or x11.

\033[1mMORE INFO\033[0m
    \tAuthor:\tauthor_label
    \tEmail:\temail_label
    \tSource:\tgithub_label
\033[1mVERSION\033[0m
    \tversion_label
"""

import os
import re
import sys
import shlex
import shutil
from subprocess import run
from textwrap import dedent

from rsfpy.tools import Msvgviewer as svgviewer
from rsfpy.utils import _get_stdname, _str_match_re
from rsfpy.version import __version__, __email__, __author__, __github__


__progname__ = os.path.basename(sys.argv[0])

__doc__ = __doc__.replace("author_label", __author__)
__doc__ = __doc__.replace("email_label", __email__)
__doc__ = __doc__.replace("github_label", __github__)
__doc__ = __doc__.replace("version_label", __version__)

DOC = dedent(__doc__.replace("Mvplviewer.py", __progname__))

CONVERTER_PREFIXES = (
    "font",
    "label",
    "title",
    "barlabel",
    "scalebar",
    "frame",
    "axis",
    "grid",
)

CONVERTER_SUFFIXES = (
    "color",
    "col",
    "sz",
    "size",
    "fat",
    "width",
    "weight",
)

CONVERTER_OPTION_NAMES = {"bgcolor", "border", "font", "fontfamily"}

for _prefix in CONVERTER_PREFIXES:
    for _suffix in CONVERTER_SUFFIXES:
        CONVERTER_OPTION_NAMES.add(_prefix + _suffix)
    CONVERTER_OPTION_NAMES.add(_prefix + "family")

CONVERTER_OPTION_FLAGS = {"--" + name for name in CONVERTER_OPTION_NAMES}


def strip_split_markers(svg):
    return re.sub(br'(?m)^<!-- RSFPY_SPLIT[^\n]*-->\n?', b"", svg)


def split_svg_frames(svg):
    parts = re.split(br'(?=<!-- RSFPY_SPLIT\b)', svg)
    frames = []
    for part in parts:
        part = part.strip()
        if not part:
            continue
        part = strip_split_markers(part).strip()
        if part:
            frames.append(part + b"\n")
    if not frames and svg.strip():
        frames.append(strip_split_markers(svg).strip() + b"\n")
    return frames


def converter_command():
    override = os.environ.get("RSFPY_VPLVIEWER_CONVERTER")
    if override:
        cmd = svgviewer.split_cmd(override)
        if cmd:
            return cmd

    bundled = svgviewer.resource_path("vpl2svg")
    if svgviewer.is_executable(bundled):
        return [str(bundled)]

    path = shutil.which("vpl2svg")
    if path:
        return [path]

    raise RuntimeError(
        "vpl2svg converter not found. Build native tools or set "
        "RSFPY_VPLVIEWER_CONVERTER."
    )


def read_vpl_sources(args, stdin_data):
    sources = []
    stdin_used = False

    for item in args:
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


def converter_supports_path_arg(cmd):
    if not cmd:
        return False
    return os.path.basename(cmd[0]).lower().startswith("vpl2svg")


def convert_one_vpl(cmd, converter_args, title, data, source_path):
    use_path = (
        source_path != "stdin"
        and os.path.exists(source_path)
        and converter_supports_path_arg(cmd)
    )

    result = run(
        cmd + converter_args + [source_path] if use_path else cmd + converter_args,
        input=None if use_path else data,
        stdout=-1,
        stderr=-1,
    )

    if result.stderr:
        try:
            sys.stderr.buffer.write(result.stderr)
        except Exception:
            sys.stderr.write(result.stderr.decode("utf-8", "replace"))

    if result.returncode != 0:
        raise RuntimeError(
            "VPL conversion failed for %s with code %s"
            % (source_path, result.returncode)
        )

    svg = result.stdout
    if b"<svg" not in svg[:4096]:
        raise RuntimeError("VPL conversion did not produce SVG for %s" % source_path)

    return svg


def build_svg_payload(args, stdin_data, converter_args):
    sources = read_vpl_sources(args, stdin_data)
    if not sources:
        raise RuntimeError("no VPL input")

    cmd = converter_command()
    chunks = []

    for title, data, source_path in sources:
        svg = convert_one_vpl(cmd, converter_args, title, data, source_path)
        chunks.append(svgviewer.frame_marker(title, source_path))
        chunks.append(svg)
        chunks.append(b"\n")

    if len(sources) == 1:
        return sources[0][0], b"".join(chunks)

    return "sequence_%d" % len(sources), b"".join(chunks)


def send_payload_with_client(title, payload):
    cmd_base = svgviewer.client_sender_command()

    if not svgviewer.command_exists(cmd_base):
        sys.stderr.write(
            "Client sender not found: %s\n"
            % " ".join(shlex.quote(x) for x in cmd_base)
        )
        return 127

    try:
        host, port, token, cfg_path = svgviewer.client_connection_options()
        cmd = list(cmd_base)
        cmd += ["--host", host, "--port", str(port)]
        if token:
            cmd += ["--token", token]
        if title:
            cmd += ["--title", title]

        if cfg_path:
            sys.stderr.write("Using rsfclient pair config: %s\n" % cfg_path)

        result = run(cmd, input=payload, capture_output=False)

        if result.returncode != 0 and cfg_path:
            svgviewer.remove_stale_pair_config(cfg_path)

        return result.returncode

    except Exception as exc:
        sys.stderr.write("Client sender failed: %s\n" % exc)
        return 1


def run_svg_payload_viewer(backend, title, payload):
    tried = []

    for name, path in svgviewer.get_backend_order(backend):
        if name == "CLIENT":
            ret = send_payload_with_client(title, payload)
            if ret == 0:
                return 0

            tried.append("CLIENT: exited with code %s: %s" % (
                ret,
                " ".join(shlex.quote(x) for x in svgviewer.client_sender_command()),
            ))

            if backend == "client":
                return ret

            continue

        if not svgviewer.is_executable(path):
            tried.append("%s: not found or not executable: %s" % (name, path))
            continue

        result = run(
            [str(path)],
            input=payload,
            capture_output=False,
            env=svgviewer.viewer_env_for_backend(name),
        )

        if result.returncode == 0:
            return 0

        tried.append("%s: exited with code %s: %s" % (
            name,
            result.returncode,
            path,
        ))

        return result.returncode

    sys.stderr.write("No usable SVG viewer backend succeeded.\n")
    for item in tried:
        sys.stderr.write("  %s\n" % item)

    return 1


def show_help(exit_code=0):
    try:
        run(["less", "-R"], input=DOC.encode())
    except Exception:
        sys.stdout.write(DOC)
    sys.exit(exit_code)


def split_vpl_args(items, *, allow_files=True):
    files = []
    converter_args = []
    i = 0

    while i < len(items):
        item = items[i]
        if item.startswith("--"):
            if (
                "=" not in item
                and i + 1 < len(items)
                and not items[i + 1].startswith("--")
                and "=" not in items[i + 1]
            ):
                i += 2
            else:
                i += 1
            continue

        if "=" in item and not os.path.exists(item):
            parsed = _str_match_re([item])
            key, value = next(iter(parsed.items()), ("", ""))
            if key in CONVERTER_OPTION_NAMES:
                converter_args.extend(["--" + key, value])
            i += 1
            continue

        if allow_files and not item.startswith("--"):
            files.append(item)
        i += 1

    return files, converter_args


def collect_env_converter_args():
    raw = os.environ.get("RSFVPLOPTS", "")
    if not raw.strip():
        return []
    parsed = _str_match_re(raw)
    return [
        item
        for key, value in parsed.items()
        for item in (("--" + key), value)
        if key in CONVERTER_OPTION_NAMES
    ]


def main():
    backend, raw_args, help_requested = svgviewer.parse_common_cli()

    if help_requested:
        show_help(0)

    args, key_value_converter_args = split_vpl_args(raw_args)
    converter_args = collect_env_converter_args() + key_value_converter_args
    stdin_data = None

    stdname = _get_stdname()
    if (
        stdname[1]
        and os.path.exists(stdname[1])
        and stdname[1].lower().endswith((".v", ".vpl"))
    ):
        args = [stdname[1]] + args
    elif not sys.stdin.isatty():
        stdin_data = sys.stdin.buffer.read()

    args = svgviewer.readable_input_args(args)

    try:
        title, payload = build_svg_payload(args, stdin_data, converter_args)
        ret = run_svg_payload_viewer(backend, title, payload)
    except Exception as exc:
        sys.stderr.write("vplviewer: %s\n" % exc)
        ret = 1

    sys.exit(ret)


if __name__ == "__main__":
    main()
