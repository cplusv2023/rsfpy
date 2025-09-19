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

svgviewer_path = importlib.resources.files(bin).joinpath("svgviewer")
__progname__ = os.path.basename(sys.argv[0])
DOC = dedent(__doc__.replace('Msvgpen.py', __progname__))



def main():
    if len(sys.argv) < 2 and sys.stdin.isatty():
        run(['less', '-R'], input=DOC.encode())
        sys.exit(1)
    else:
        run([str(svgviewer_path)] + sys.argv[1:])