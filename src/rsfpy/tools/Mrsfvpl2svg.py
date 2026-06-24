#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__doc__ = """
\033[1mNAME\033[0m
    \tMrsfvpl2svg.py
\033[1mDESCRIPTION\033[0m
    \tConvert VPL images to SVG without opening a viewer.

\033[1mSYNOPSIS\033[0m
    \tMrsfvpl2svg.py [conversion options] [standard=y|n] [cat=y|n] [< in.vpl] in.vpl [out.svg] ...

\033[1mRULES\033[0m
    \tkey=value arguments are converter/options.
    \tNon key=value arguments are input files, except that a following .svg
    \targument is used as the output path for the previous input.

    \tstandard=n
    \t    Default. Preserve rsfpy's custom multi-frame SVG sequence markers.

    \tstandard=y
    \t    Write ordinary SVG files. For multi-frame VPL files, write
    \t    basename_1.svg, basename_2.svg, ...

    \tcat=y
    \t    Highest priority. Convert all frames from all inputs to one rsfpy
    \t    multi-frame SVG sequence on stdout.
\033[1mVERSION\033[0m
    \tversion_label
"""

import os
import re
import sys
from textwrap import dedent
from subprocess import run

from rsfpy.tools import Msvgviewer as svgviewer
from rsfpy.tools import Mvplviewer as vplviewer
from rsfpy.utils import _get_stdname, _str_match_re
from rsfpy.version import __version__


__progname__ = os.path.basename(sys.argv[0])
__doc__ = __doc__.replace("version_label", __version__)
DOC = dedent(__doc__.replace("Mrsfvpl2svg.py", __progname__))

STDOUT_TTY_MESSAGE = "You don't want to dump plain svg-text to terminal."


def show_help(exit_code=0):
    try:
        run(["less", "-R"], input=DOC.encode())
    except Exception:
        sys.stdout.write(DOC)
    sys.exit(exit_code)


def truthy(value):
    return str(value).strip().lower() in ("1", "y", "yes", "true", "on")


def svg_output_name(path):
    base = os.path.basename(path)
    root, _ = os.path.splitext(base)
    return (root or "figure") + ".svg"


def frame_output_name(path, frame_index):
    root, ext = os.path.splitext(path)
    return "%s_%d%s" % (root, frame_index, ext or ".svg")


def split_cli(argv=None):
    _, raw_args, help_requested = svgviewer.parse_common_cli(argv)
    if help_requested:
        show_help(0)

    controls = {"standard": "n", "cat": "n"}
    converter_tokens = []
    files = []

    for item in raw_args:
        if "=" not in item or os.path.exists(item):
            files.append(item)
            continue

        parsed = _str_match_re([item])
        key, value = next(iter(parsed.items()), ("", ""))
        if key in controls:
            controls[key] = value
        elif key in vplviewer.CONVERTER_OPTION_NAMES:
            converter_tokens.append(item)

    _, cli_converter_args = vplviewer.split_vpl_args(converter_tokens)
    converter_args = vplviewer.collect_env_converter_args() + cli_converter_args
    return files, converter_args, truthy(controls["standard"]), truthy(controls["cat"])


def plan_file_pairs(files):
    pairs = []
    i = 0
    while i < len(files):
        inp = files[i]
        if inp == "-":
            pairs.append(("-", None))
            i += 1
            continue

        out = None
        if i + 1 < len(files) and files[i + 1].lower().endswith(".svg"):
            out = files[i + 1]
            i += 2
        else:
            out = svg_output_name(inp)
            i += 1
        pairs.append((inp, out))
    return pairs


def read_source(path, stdin_data):
    if path == "-":
        if stdin_data is None:
            raise RuntimeError("'-' was specified but stdin is empty")
        return "stdin", stdin_data, "stdin"

    with open(path, "rb") as f:
        data = f.read()
    title = os.path.splitext(os.path.basename(path))[0] or "figure"
    return title, data, path


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


def sequence_frames(title, source_path, svg):
    frames = split_svg_frames(svg)
    chunks = []
    total = len(frames)

    for index, frame in enumerate(frames, 1):
        label = title if total == 1 else "%s #%d" % (title, index)
        chunks.append(svgviewer.frame_marker(label, source_path))
        chunks.append(frame)
        chunks.append(b"\n")

    return chunks


def write_stdout(data):
    if sys.stdout.isatty():
        raise RuntimeError(STDOUT_TTY_MESSAGE)
    sys.stdout.buffer.write(data)


def write_file(path, data):
    with open(path, "wb") as f:
        f.write(data)


def convert_source(cmd, converter_args, title, data, source_path):
    return vplviewer.convert_one_vpl(cmd, converter_args, title, data, source_path)


def main():
    try:
        files, converter_args, standard, cat = split_cli()
        stdin_data = None

        stdname = _get_stdname()
        if (
            not files
            and stdname[1]
            and os.path.exists(stdname[1])
            and stdname[1].lower().endswith((".v", ".vpl"))
        ):
            files = [stdname[1]]
        elif not sys.stdin.isatty():
            stdin_data = sys.stdin.buffer.read()

        if not files and stdin_data is None:
            raise RuntimeError("no VPL input")

        pairs = plan_file_pairs(files) if files else [("-", None)]
        cmd = vplviewer.converter_command()

        if cat:
            chunks = []
            for inp, _ in pairs:
                title, data, source_path = read_source(inp, stdin_data)
                svg = convert_source(cmd, converter_args, title, data, source_path)
                chunks.extend(sequence_frames(title, source_path, svg))
            write_stdout(b"".join(chunks))
            return

        for inp, out in pairs:
            title, data, source_path = read_source(inp, stdin_data)
            svg = convert_source(cmd, converter_args, title, data, source_path)

            if inp == "-":
                frames = split_svg_frames(svg) if standard else [svg]
                write_stdout(frames[0] if standard else svg)
                continue

            if standard:
                frames = split_svg_frames(svg)
                if len(frames) <= 1:
                    write_file(out, frames[0] if frames else b"")
                else:
                    for index, frame in enumerate(frames, 1):
                        write_file(frame_output_name(out, index), frame)
            else:
                write_file(out, svg)

    except Exception as exc:
        sys.stderr.write("rsfvpl2svg: %s\n" % exc)
        sys.exit(1)


if __name__ == "__main__":
    main()
