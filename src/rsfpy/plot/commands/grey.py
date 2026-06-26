# -*- coding: utf-8 -*-
"""Standalone ``rsfgrey`` command implementation."""

import io
import sys

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FormatStrFormatter, ScalarFormatter, MaxNLocator

from rsfpy import Rsfarray
from rsfpy.version import __SVG_SPLITTER
from rsfpy.plot.movie import MovieFrame
from rsfpy.plot.movie.grey import GreyMovieUpdater
from rsfpy.plot.display import estimate_gain, make_colormap
from .common import (
    PlotCommandContext, add_overlays, bool_param, configure_matplotlib,
    create_figure, decorate_axes, error, float_param, save_figure, warning,
    show_documentation, wants_documentation,
)
from .io import read_stdin_rsf


def _frame_label(prefix, suffix, axis, index):
    return "%s%s: %5g of %5g" % (prefix, suffix, axis[index], axis[-1])


def _splitter(label):
    return __SVG_SPLITTER[:-3] + 'framelabel="%s"' % label + __SVG_SPLITTER[-3:]


def _bar_data(params, data, scalebar):
    if data.dtype != np.uint8 or not scalebar:
        return None
    path = params.get("barfile", params.get("bar"))
    if path is None:
        error("Error reading bar= when scalebar=y, no bar file supplied.")
    try:
        values = np.asarray(Rsfarray(path).window(n3=1)).reshape(-1)
        if values.size < 8:
            error("Error reading bar= when scalebar=y, bar file is too short.")
        minval, maxval = np.frombuffer(values[:8].astype(np.uint8, copy=False).tobytes(),
                                       dtype=np.float32)[:2]
        return values, float(minval), float(maxval)
    except Exception as exc:
        error("Error reading bar= when scalebar=y, %s" % exc)


def _make_colorbar(figure, axes, data, params, cmap, bar_data, context):
    if not bool_param(params, "scalebar", False):
        return None
    colorbar = figure.colorbar(axes.images[0], ax=axes)
    formatbar = params.get("formatbar")
    if bar_data is not None:
        values, minval, maxval = bar_data
        colorbar.ax.clear()
        colorbar.ax.imshow(255 - values[8:, np.newaxis], aspect="auto", cmap=cmap,
                           vmin=0, vmax=255, extent=[0, 1, minval, maxval])
        colorbar.ax.xaxis.set_visible(False)
        colorbar.ax.yaxis.set_label_position("right")
        colorbar.ax.yaxis.set_major_formatter(FormatStrFormatter(formatbar) if formatbar else ScalarFormatter())
        colorbar.ax.yaxis.set_major_locator(MaxNLocator(nbins=float_param(params, "nticbar", 5)))
    elif formatbar:
        colorbar.ax.yaxis.set_major_formatter(FormatStrFormatter(formatbar))
    minval = float_param(params, "minval", None)
    maxval = float_param(params, "maxval", None)
    if minval is not None or maxval is not None:
        vmin, vmax = axes.images[0].get_clim()
        colorbar.ax.set_ylim(vmin if minval is None else minval, vmax if maxval is None else maxval)
    return colorbar


def _render_first_frame(axes, data, params, cmap, gain):
    data.grey(ax=axes,
              transp=bool_param(params, "transp", True),
              yreverse=bool_param(params, "yreverse", True),
              xreverse=bool_param(params, "xreverse", False),
              allpos=bool_param(params, "allpos", False),
              clip=float_param(params, "clip", None),
              pclip=float_param(params, "pclip", 99.0),
              bias=float_param(params, "bias", 0.0), cmap=cmap,
              gain=gain,
              max_pixels=float_param(params, "maxpixels", None),
              min1=float_param(params, "min1", None), max1=float_param(params, "max1", None),
              min2=float_param(params, "min2", None), max2=float_param(params, "max2", None),
              colorbar=False, show=False, interpolation="none")


def _gain_for_frame(params, reference):
    return estimate_gain(reference, clip=float_param(params, "clip", None),
                         pclip=float_param(params, "pclip", 99.0),
                         bias=float_param(params, "bias", 0.0),
                         mean=bool_param(params, "mean", False),
                         allpos=bool_param(params, "allpos", False),
                         gpow=float_param(params, "gpow", 1.0),
                         polarity=bool_param(params, "polarity", False))


def _gain_reference(params, frames, movie):
    if not movie:
        return frames.window(n3=1, f3=0, copy=False), False
    panel = str(params.get("gainpanel", "0")).lower()
    if panel.startswith("a"):
        return frames, False
    if panel.startswith("e"):
        return None, True
    try:
        index = max(0, min(frames.n3 - 1, int(panel) - 1))
    except ValueError:
        index = 0
    return frames.window(n3=1, f3=index, copy=False), False


def main(argv=None):
    """Render Madagascar RSF input as a grey plot without using Mpygrey."""

    args = list(sys.argv[1:] if argv is None else argv)
    if wants_documentation(args):
        show_documentation("grey")
        return 0
    context = PlotCommandContext("grey", args)
    params = context.params
    if sys.stdin.isatty():
        error("Error: no input data?")
    try:
        data = read_stdin_rsf()
    except (TypeError, ValueError) as exc:
        error("Error: %s." % exc)
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
    cmap = make_colormap(params.get("color", "gray"))
    data.sfput(label1=params.get("label1", data.label1), unit1=params.get("unit1", data.unit1))
    data.sfput(label2=params.get("label2", data.label2), unit2=params.get("unit2", data.unit2))
    figure, axes, dpi = create_figure(context)
    bar_data = _bar_data(params, data, bool_param(params, "scalebar", False))

    movie = bool_param(params, "movie", True) and context.output_format.lower() == "svg"
    frames = data
    nframes = frames.n3 if movie else 1
    maxframe = int(float_param(params, "maxframe", 300))
    step = max(1, int(nframes / maxframe)) if nframes > maxframe else 1
    count = min(nframes, maxframe)
    movie = movie and nframes > 1
    if bool_param(params, "movie", True) and context.output_format.lower() != "svg":
        warning("Movie mode only supports svg output, got format=%s." % context.output_format)

    first = frames.window(n3=1, f3=0, copy=False)
    gain_reference, gain_each = _gain_reference(params, frames, movie)
    first_gain = _gain_for_frame(params, first if gain_each else gain_reference)
    _render_first_frame(axes, first, params, cmap, first_gain)
    colorbar = _make_colorbar(figure, axes, first, params, cmap, bar_data, context)
    decorate_axes(context, axes, title=params.get("title", data.header.get("title", "")),
                  format1=params.get("format1"), format2=params.get("format2"),
                  ntic1=float_param(params, "ntic1", 5), ntic2=float_param(params, "ntic2", 5),
                  colorbar=colorbar, barlabel=params.get("barlabel", ""),
                  barunit=params.get("barunit", params.get("unitbar")),
                  barlabelsz=float_param(params, "barlabelsz", context.label_style.fontsize),
                  barlabelfat=params.get("barlabelfat"))
    add_overlays(context, axes)
    plt.tight_layout()

    if sys.stdout.isatty():
        if movie:
            warning("No out, set movie=n and show figure.")
        plt.show()
        plt.close(figure)
        return 0

    if not movie:
        save_figure(figure, sys.stdout.buffer, context.output_format, dpi)
        sys.stdout.buffer.flush()
        plt.close(figure)
        return 0

    updater = GreyMovieUpdater()
    output = io.StringIO()
    prefix = data.label3 or "Frame"
    suffix = " (%s)" % data.unit3 if data.unit3 else ""
    first_label = _frame_label(prefix, suffix, frames.axis3, 0)
    output.write("\n%s\n" % _splitter(first_label))
    save_figure(figure, output, "svg", dpi)
    first_svg = output.getvalue()
    template = updater.build_template(first_svg, context)
    sys.stdout.write(first_svg)
    sys.stdout.flush()

    for iframe in range(1, count):
        index = iframe * step
        frame = frames.window(n3=1, f3=index, copy=False)
        vmin, vmax = axes.images[0].get_clim()
        frame_gain = _gain_for_frame(params, frame) if gain_each else first_gain
        state = MovieFrame(index=index, payload=frame, clip=frame_gain.clip,
                           bias=frame_gain.bias, allpos=frame_gain.allpos,
                           cmap=cmap, dpi=dpi, gain=frame_gain)
        template = updater.update_frame(template, state, context)
        warning("Frame %d of %d;" % (iframe + 1, count))
        sys.stdout.write("\n%s\n%s" % (_splitter(_frame_label(prefix, suffix, frames.axis3, index)), template.svg))
        sys.stdout.flush()
    sys.stderr.write("\n")
    plt.close(figure)
    return 0
