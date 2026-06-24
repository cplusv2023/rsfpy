# -*- coding: utf-8 -*-
"""Standalone ``rsfwiggle`` command implementation."""

import sys

import matplotlib.pyplot as plt
import numpy as np

from rsfpy import Rsfarray
from rsfpy.version import __SVG_SPLITTER
from .common import (
    PlotCommandContext, add_overlays, bool_param, configure_matplotlib,
    create_figure, decorate_axes, error, float_param, save_figure, warning,
)
from .io import read_stdin_rsf


def _splitter(label):
    return __SVG_SPLITTER[:-3] + 'framelabel="%s"' % label + __SVG_SPLITTER[-3:]


def _render(context, data, xpos_data=None):
    params = context.params
    figure, axes, dpi = create_figure(context)
    line_color = params.get("lcolor", "k")
    ncolor = params.get("ncolor", "none")
    pcolor = params.get("pcolor", line_color)
    if not bool_param(params, "fill", True):
        ncolor = pcolor = "none"
    data.wiggle(ax=axes,
                transp=bool_param(params, "transp", True),
                yreverse=bool_param(params, "yreverse", True),
                xreverse=bool_param(params, "xreverse", False),
                min1=float_param(params, "min1", None), max1=float_param(params, "max1", None),
                min2=float_param(params, "min2", None), max2=float_param(params, "max2", None),
                zplot=float_param(params, "zplot", 1.0), bias=float_param(params, "bias", 0.0),
                clip=float_param(params, "clip", None), pclip=float_param(params, "pclip", 99.0),
                ncolor=ncolor, pcolor=pcolor, lcolor=line_color,
                linewidth=float_param(params, "plotfat", context.frame_style.width or 1.0),
                xpos=xpos_data, show=False)
    decorate_axes(context, axes, title=params.get("title", data.header.get("title", "")),
                  format1=params.get("format1"), format2=params.get("format2"),
                  ntic1=float_param(params, "ntic1", 5), ntic2=float_param(params, "ntic2", 5))
    add_overlays(context, axes)
    plt.tight_layout()
    return figure, dpi


def main(argv=None):
    context = PlotCommandContext("wiggle", list(sys.argv[1:] if argv is None else argv))
    params = context.params
    if sys.stdin.isatty():
        error("Error: no input data?")
    try:
        data = read_stdin_rsf()
    except (TypeError, ValueError) as exc:
        error("Error: %s." % exc)
    if data.dtype == np.uint8:
        error("Error: wiggle plot does not support uchar data.")
    if data.dtype == np.int32:
        warning("Got %s, converting to float32." % data.dtype)
        data = Rsfarray(data.astype(np.float32), header=data.header)
    elif data.dtype == np.complex64:
        warning("Got %s, converting to float32 using abs." % data.dtype)
        data = Rsfarray(np.abs(data), header=data.header)
    if data.ndim < 2:
        data = data.reshape((data.n1, 1, 1))
    else:
        data = data.reshape((data.n1, data.n2, -1))

    configure_matplotlib(context)
    data.sfput(label1=params.get("label1", data.label1), unit1=params.get("unit1", data.unit1))
    data.sfput(label2=params.get("label2", data.label2), unit2=params.get("unit2", data.unit2))
    xpos = params.get("offset", params.get("xpos"))
    xpos_data = Rsfarray(xpos) if xpos is not None else None
    movie = bool_param(params, "movie", True) and data.n3 > 1 and context.output_format.lower() == "svg"
    maxframe = int(float_param(params, "maxframe", 300))
    step = max(1, int(data.n3 / maxframe)) if data.n3 > maxframe else 1
    count = min(data.n3, maxframe) if movie else 1
    if sys.stdout.isatty():
        figure, _dpi = _render(context, data.window(n3=1, f3=0, copy=False), xpos_data)
        plt.show()
        plt.close(figure)
        return 0
    for iframe in range(count):
        index = iframe * step
        frame = data.window(n3=1, f3=index, copy=False)
        figure, dpi = _render(context, frame, xpos_data)
        if movie:
            label = data.label3 or "Frame"
            unit = " (%s)" % data.unit3 if data.unit3 else ""
            sys.stdout.write("\n%s\n" % _splitter("%s%s: %5g of %5g" % (label, unit, data.axis3[index], data.axis3[-1])))
            save_figure(figure, sys.stdout, "svg", dpi)
            if iframe:
                warning("Frame %d of %d;" % (iframe + 1, count))
        else:
            save_figure(figure, sys.stdout.buffer, context.output_format, dpi)
        plt.close(figure)
    sys.stdout.flush()
    if movie:
        sys.stderr.write("\n")
    return 0
