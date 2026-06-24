# -*- coding: utf-8 -*-
"""Shared I/O helpers for RSFPY plotting commands."""

import os
import sys

import numpy as np

from rsfpy import Rsfarray
from rsfpy.utils import _get_stdname


SUPPORTED_OUTPUT_SUFFIXES = {
    ".png",
    ".jpg",
    ".jpeg",
    ".gif",
    ".bmp",
    ".tif",
    ".tiff",
    ".webp",
    ".webm",
    ".eps",
    ".ps",
    ".pgf",
    ".svg",
}

SUPPORTED_RSF_DTYPES = {
    np.dtype(np.int32),
    np.dtype(np.float32),
    np.dtype(np.complex64),
    np.dtype(np.uint8),
}


def output_suffix(default=".svg"):
    stdname = _get_stdname()
    path = stdname[1]
    if path is None:
        return default
    suffix = os.path.splitext(path)[1].lower()
    return suffix if suffix in SUPPORTED_OUTPUT_SUFFIXES else default


def output_format(params, default_suffix=None):
    suffix = default_suffix or output_suffix()
    return params.get("format", suffix.lstrip("."))


def read_stdin_rsf(stdin=None):
    stream = stdin if stdin is not None else sys.stdin.buffer
    data = Rsfarray(stream)
    if data.size == 0:
        raise ValueError("failed read RSF data from input")
    dtype = np.dtype(data.dtype)
    if dtype not in SUPPORTED_RSF_DTYPES:
        raise TypeError("unsupported RSF data type: %s" % dtype)
    return data
