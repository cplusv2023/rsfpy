#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__doc__ = """
\033[1mNAME\033[0m
    \tMsvgviewer.py
\033[1mDESCRIPTION\033[0m
    \tView SVG images.
\033[1mSYNOPSIS\033[0m
    \tMsvgviewer.py [--backend gtk|x11|auto] [< in0.svg] in1.svg in2.svg ...
\033[1mMORE INFO\033[0m
    \tAuthor:\tauthor_label
    \tEmail:\temail_label
    \tSource:\tgithub_label
\033[1mVERSION\033[0m
    \tversion_label
"""

import os
import sys
import argparse
from subprocess import run
from textwrap import dedent
import importlib.resources

from rsfpy import bin
from rsfpy.utils import _get_stdname
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


def is_executable(path):
    try:
        return path.is_file() and os.access(str(path), os.X_OK)
    except Exception:
        return False


def build_parser():
    parser = argparse.ArgumentParser(
        prog=__progname__,
        add_help=False,
        description="View SVG images.",
    )

    parser.add_argument(
        "--backend",
        choices=["auto", "gtk", "x11"],
        default=os.environ.get("RSFPY_SVGVIEWER_BACKEND", "auto"),
        help="Select SVG viewer backend: auto, gtk, or x11. "
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


def get_backend_order(backend):
    if backend == "gtk":
        return [("GTK", svgviewer_path_gtk)]
    if backend == "x11":
        return [("X11", svgviewer_path_x11)]
    return [
        ("GTK", svgviewer_path_gtk),
        ("X11", svgviewer_path_x11),
    ]


def run_viewer(backend, args, stdin_data):
    tried = []

    for name, path in get_backend_order(backend):
        if not is_executable(path):
            tried.append(f"{name}: not found or not executable: {path}")
            continue

        result = run(
            [str(path)] + args,
            input=stdin_data,
            capture_output=False,
        )

        if result.returncode == 0:
            return 0

        tried.append(f"{name}: exited with code {result.returncode}: {path}")

        # 用户显式指定后端时，不自动 fallback
        if backend in ("gtk", "x11"):
            return result.returncode

    sys.stderr.write("No usable SVG viewer backend succeeded.\n")
    for item in tried:
        sys.stderr.write(f"  {item}\n")

    return 1


def main():
    parser = build_parser()
    ns, unknown = parser.parse_known_args()

    if ns.help or (len(sys.argv) < 2 and sys.stdin.isatty()):
        run(["less", "-R"], input=DOC.encode())
        sys.exit(0 if ns.help else 1)

    args = ns.files + unknown
    stdin_data = None

    # in case pseudo shell
    stdname = _get_stdname()
    if stdname[1] and os.path.exists(stdname[1]) and stdname[1].endswith(".svg"):
        args = [stdname[1]] + args
    elif not sys.stdin.isatty():
        stdin_data = sys.stdin.buffer.read()

    if not args and stdin_data is None:
        run(["less", "-R"], input=DOC.encode())
        sys.exit(1)

    ret = run_viewer(ns.backend, args, stdin_data)
    sys.exit(ret)


if __name__ == "__main__":
    main()