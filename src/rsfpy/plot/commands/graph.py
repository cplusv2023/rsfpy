# -*- coding: utf-8 -*-
"""Standalone ``rsfgraph`` command implementation, including frame movies."""

import re
import sys

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import LogLocator

from rsfpy import Rsfarray
from rsfpy.version import __SVG_SPLITTER
from .common import (
    PlotCommandContext, add_overlays, bool_param, configure_matplotlib,
    create_figure, decorate_axes, error, float_param, save_figure, warning,
    show_documentation, wants_documentation,
)
from .io import read_stdin_rsf


def _cycle(value, count, default):
    values = list(default) if value is None else re.split(r"[ ,;]+", str(value))
    while len(values) < count:
        values.extend(values)
    return values[:count]


def _splitter(label):
    return __SVG_SPLITTER[:-3] + 'framelabel="%s"' % label + __SVG_SPLITTER[-3:]


def _render(context, data):
    params = context.params
    figure, axes, dpi = create_figure(context)
    count = data.n2
    colors = _cycle(params.get("lcolors", params.get("lcolor")), count,
                    ["k", "r", "g", "b", "c", "m", "y"])
    styles = _cycle(params.get("lstyles", params.get("lstyle", params.get("linestyle"))), count,
                    ["-", "--", "-.", ":"])
    stem = bool_param(params, "stem", False)
    marker = params.get("markers", params.get("marker", params.get("symbols", params.get("symbol"))))
    markers = _cycle(marker, count, [".", ",", "o", "*", "s", "p", "x", "d", "v", "^", "<", ">"] if stem else ["none"])
    width = float_param(params, "plotfat", context.frame_style.width or 1.0)
    size = float_param(params, "markersize", float_param(params, "markersz", context.font_style.fontsize or 12.0))
    transp = bool_param(params, "transp", False)
    for index in range(count):
        values = np.asarray(data[:, index]).reshape(-1)
        if data.dtype == np.complex64:
            x, y = np.real(values), np.imag(values)
        elif transp:
            x, y = values, data.axis1
        else:
            x, y = data.axis1, values
        if stem:
            container = axes.stem(x, y, linefmt=colors[index] + "-",
                                  markerfmt=colors[index] + markers[index],
                                  basefmt=(context.frame_style.color or "k") + styles[index])
            container.markerline.set_markersize(size)
            container.stemlines.set_linewidth(width)
        else:
            axes.plot(x, y, color=colors[index], linewidth=width, linestyle=styles[index],
                      marker=markers[index], markersize=size)
    if transp:
        default1, default2 = axes.get_ylim(), axes.get_xlim()
    else:
        default1, default2 = axes.get_xlim(), axes.get_ylim()
    min1, max1 = float_param(params, "min1", default1[0]), float_param(params, "max1", default1[1])
    min2, max2 = float_param(params, "min2", default2[0]), float_param(params, "max2", default2[1])
    logx, logy = bool_param(params, "logx", False), bool_param(params, "logy", False)
    logxbase, logybase = float_param(params, "logxbase", 10.0), float_param(params, "logybase", 10.0)
    if logx and (logxbase <= 0 or (min2 if transp else min1) <= 0):
        warning("Warning: invalid logarithmic x axis, use normal x axis.")
        logx = False
    if logy and (logybase <= 0 or (min1 if transp else min2) <= 0):
        warning("Warning: invalid logarithmic y axis, use normal y axis.")
        logy = False
    if logx:
        axes.set_xscale("log", base=logxbase)
    if logy:
        axes.set_yscale("log", base=logybase)
    if transp:
        axes.set_xlim(max2, min2) if bool_param(params, "xreverse", False) else axes.set_xlim(min2, max2)
        axes.set_ylim(max1, min1) if bool_param(params, "yreverse", False) else axes.set_ylim(min1, max1)
        axes.set_ylabel(data.label_unit(0))
        axes.set_xlabel(data.label_unit(1))
    else:
        axes.set_xlim(max1, min1) if bool_param(params, "xreverse", False) else axes.set_xlim(min1, max1)
        axes.set_ylim(max2, min2) if bool_param(params, "yreverse", False) else axes.set_ylim(min2, max2)
        axes.set_xlabel(data.label_unit(0))
        axes.set_ylabel(data.label_unit(1))
    decorate_axes(context, axes, title=params.get("title", data.header.get("title", "")),
                  format1=params.get("format1"), format2=params.get("format2"),
                  ntic1=float_param(params, "ntic1", 5), ntic2=float_param(params, "ntic2", 5))
    if logx:
        axes.xaxis.set_major_locator(LogLocator(base=logxbase, numticks=float_param(params, "ntic2", 5)))
    if logy:
        axes.yaxis.set_major_locator(LogLocator(base=logybase, numticks=float_param(params, "ntic1", 5)))
    add_overlays(context, axes)
    plt.tight_layout()
    return figure, dpi


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if wants_documentation(args):
        show_documentation("graph")
        return 0
    context = PlotCommandContext("graph", args)
    params = context.params
    if sys.stdin.isatty():
        error("Error: no input data?")
    try:
        data = read_stdin_rsf()
    except (TypeError, ValueError) as exc:
        error("Error: %s." % exc)
    if data.dtype == np.uint8:
        error("Error: graph plot does not support uchar data.")
    if data.dtype == np.int32:
        warning("Got %s, converting to float32." % data.dtype)
        data = Rsfarray(data.astype(np.float32), header=data.header)
    if data.ndim < 2:
        data = data.reshape((data.n1, 1, 1))
    else:
        data = data.reshape((data.n1, data.n2, -1))
    data.sfput(label1=params.get("label1", data.label1), unit1=params.get("unit1", data.unit1))
    data.sfput(label2=params.get("label2", data.label2), unit2=params.get("unit2", data.unit2))
    configure_matplotlib(context)
    movie = bool_param(params, "movie", True) and data.n3 > 1 and context.output_format.lower() == "svg"
    maxframe = int(float_param(params, "maxframe", 300))
    step = max(1, int(data.n3 / maxframe)) if data.n3 > maxframe else 1
    count = min(data.n3, maxframe) if movie else 1
    if sys.stdout.isatty():
        figure, _dpi = _render(context, data.window(n3=1, f3=0, copy=False))
        plt.show()
        plt.close(figure)
        return 0
    for iframe in range(count):
        index = iframe * step
        frame = data.window(n3=1, f3=index, copy=False)
        figure, dpi = _render(context, frame)
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
