#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__doc__ = """
\033[1mNAME\033[0m
    \tMsvgviewer.py
\033[1mDESCRIPTION\033[0m
    \tView SVG images.
\033[1mSYNOPSIS\033[0m
    \tMsvgviewer.py [< in0.svg] in1.svg in2.svg ... 

    
"""


from subprocess import run
import sys, os
from textwrap import dedent
import importlib.resources
from rsfpy import bin
from rsfpy.utils import _get_stdname

svgviewer_path = importlib.resources.files(bin).joinpath("svgviewer")
__progname__ = os.path.basename(sys.argv[0])
DOC = dedent(__doc__.replace('Msvgviewer.py', __progname__))



def main():
    if len(sys.argv) < 2 and sys.stdin.isatty():
        run(['less', '-R'], input=DOC.encode())
        sys.exit(1)
    else:
        args = sys.argv[1:]
        stdname = _get_stdname()
        if stdname[1] and os.path.exists(stdname[1]):
            args = [stdname[1]] + args
        else:
            stdin = sys.stdin.buffer.read()
        result =  run([str(svgviewer_path)] + args,
            input=stdin if stdin else None,
            capture_output=False,
            stderr=sys.stderr,)
        sys.exit(result.returncode)