# -*- coding: utf-8 -*-
"""
rsfpy - Python tools for Madagascar RSF data file reading/writing and scientific array handling.
"""
from .io import read_rsf, write_rsf
from .array import Rsfdata
from .utils import check_input_source
from .version import __version__

__all__ = ["read_rsf", "write_rsf", "Rsfdata", "check_input_source"]
