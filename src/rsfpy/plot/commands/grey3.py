# -*- coding: utf-8 -*-
"""Standalone ``rsfgrey3`` command implementation."""

import io
import sys

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FormatStrFormatter, MaxNLocator, ScalarFormatter

from rsfpy import Rsfarray
from rsfpy.plot.style import normalize_fontweight
from rsfpy.version import __SVG_SPLITTER
from rsfpy.plot.movie import MovieFrame
from rsfpy.plot.movie.grey3 import Grey3MovieUpdater
from rsfpy.plot.display import estimate_gain, make_colormap
from .common import (
    PlotCommandContext, bool_param, configure_matplotlib, create_figure,
    error, float_param, save_figure, warning,
)
from .io import read_stdin_rsf


def _bar_data(params, data, frame3, scalebar):
    if data.dtype != np.uint8 or not scalebar:
        return None
    path = params.get("barfile", params.get("bar"))
    if path is None:
        error("Error reading bar= when scalebar=y, no bar file supplied.")
    try:
        values = np.asarray(Rsfarray(path).window(n3=1, f3=frame3)).reshape(-1)
        return values, values[0], values[1]
    except Exception as exc:
        error("Error reading bar= when scalebar=y, %s" % exc)


def _movie_plane(data, movie, frame1, frame2, frame3):
    if movie == 1:
        return data.window(n1=1, f1=frame1, copy=False)
    if movie == 2:
        return data.window(n2=1, f2=frame2, copy=False)
    return data.window(n3=1, f3=frame3, copy=False)


def _gain_reference(data, params, movie, frame1, frame2, frame3):
    panel = str(params.get("gainpanel", "0")).lower()
    if panel.startswith("a"):
        return data, False
    if panel.startswith("e") and movie in (1, 2, 3):
        return _movie_plane(data, movie, frame1, frame2, frame3), True
    if data.ndim > 3:
        panels = np.asarray(data).reshape((data.n1, data.n2, data.n3, -1))
        try:
            index = max(0, min(panels.shape[3] - 1, int(panel) - 1))
        except ValueError:
            index = 0
        return panels[:, :, :, index], False
    return data, False


def _decorate(context, gattr, params, *, format1, format2, format3, ntic1, ntic2, ntic3):
    frame = context.frame_style
    axis = context.axis_style
    font = context.font_style
    label = context.label_style
    title = context.title_style
    color = axis.color or frame.color or "k"
    width = axis.width if axis.width is not None else (frame.width or 1.0)
    ticksize = float_param(params, "ticksz", font.fontsize or 12.0)
    tickfat = normalize_fontweight(params.get("tickfat", params.get("tickweight", font.fontweight or "normal")))
    gattr.set_title(params.get("title", ""), fontsize=title.fontsize or font.fontsize or 12.0,
                    fontweight=normalize_fontweight(title.fontweight or font.fontweight or "normal"), color=title.color or color)
    gattr.set_lines(color=color, width=width)
    gattr.set_spines(color=color, width=width)
    gattr.set_ticklabels(color=color, fontsize=ticksize, fontweight=tickfat)
    gattr.set_labels(color=label.color or color, fontsize=label.fontsize or font.fontsize or 12.0,
                     fontweight=normalize_fontweight(label.fontweight or font.fontweight or "normal"))
    if format1 is not None:
        gattr.ax1.yaxis.set_major_formatter(FormatStrFormatter(format1))
    if format2 is not None:
        gattr.ax1.xaxis.set_major_formatter(FormatStrFormatter(format2))
    if format3 is not None:
        gattr.ax2.xaxis.set_major_formatter(FormatStrFormatter(format3))
        gattr.ax3.yaxis.set_major_formatter(FormatStrFormatter(format3))
    if ntic1 is not None:
        gattr.ax1.yaxis.set_major_locator(MaxNLocator(nbins=ntic1))
    if ntic2 is not None:
        gattr.ax1.xaxis.set_major_locator(MaxNLocator(nbins=ntic2))
    if ntic3 is not None:
        gattr.ax2.xaxis.set_major_locator(MaxNLocator(nbins=ntic3))
        gattr.ax3.yaxis.set_major_locator(MaxNLocator(nbins=ntic3))
    if gattr.cax is not None:
        gattr.cax.tick_params(axis="both", which="major", width=width, colors=color)


def main(argv=None):
    context = PlotCommandContext("grey3", list(sys.argv[1:] if argv is None else argv))
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
    if data.ndim < 3:
        error("Error: grey3 plot needs at least three dimensions.")

    configure_matplotlib(context)
    data.sfput(label1=params.get("label1", data.label1), unit1=params.get("unit1", data.unit1))
    data.sfput(label2=params.get("label2", data.label2), unit2=params.get("unit2", data.unit2))
    data.sfput(label3=params.get("label3", data.label3), unit3=params.get("unit3", data.unit3))
    frame1 = max(0, min(data.n1 - 1, int(float_param(params, "frame1", 0))))
    frame2 = max(0, min(data.n2 - 1, int(float_param(params, "frame2", 0))))
    frame3 = max(0, min(data.n3 - 1, int(float_param(params, "frame3", 0))))
    figure, axes, dpi = create_figure(context)
    scalebar = bool_param(params, "scalebar", False)
    cmap = make_colormap(params.get("color", "gray"))
    movie_request = int(float_param(params, "movie", 0))
    reference, gain_each = _gain_reference(data, params, movie_request, frame1, frame2, frame3)
    gain = estimate_gain(reference, clip=float_param(params, "clip", None),
                         pclip=float_param(params, "pclip", 99.0),
                         bias=float_param(params, "bias", 0.0),
                         mean=bool_param(params, "mean", False),
                         allpos=bool_param(params, "allpos", False),
                         gpow=float_param(params, "gpow", 1.0),
                         polarity=bool_param(params, "polarity", False))
    gattr = data.grey3(ax=axes, frame1=frame1, frame2=frame2, frame3=frame3,
                       point1=float_param(params, "point1", 0.8), point2=float_param(params, "point2", 0.4),
                       colorbar=scalebar, cmap=cmap, clip=gain.clip,
                       pclip=None, bias=gain.bias,
                       allpos=bool_param(params, "allpos", False), title=params.get("title", data.header.get("title", "")),
                       n3tic=float_param(params, "ntic3", None), ntic1=float_param(params, "ntic1", 5),
                       ntic2=float_param(params, "ntic2", 5), format1=params.get("format1"),
                       format2=params.get("format2"), format3=params.get("format3"),
                       flat=bool_param(params, "flat", True),
                       gain=gain, max_pixels=float_param(params, "maxpixels", None), show=False)
    _decorate(context, gattr, params, format1=params.get("format1"), format2=params.get("format2"),
              format3=params.get("format3"), ntic1=float_param(params, "ntic1", 5),
              ntic2=float_param(params, "ntic2", 5), ntic3=float_param(params, "ntic3", None))
    bar = _bar_data(params, data, frame3, scalebar)
    if scalebar and gattr.cax is not None:
        if bar is not None:
            values, minval, maxval = bar
            gattr.cax.clear()
            gattr.cax.imshow(255 - values[8:, np.newaxis], aspect="auto", cmap=cmap,
                             vmin=0, vmax=255, extent=[0, 1, minval, maxval])
            gattr.cax.xaxis.set_visible(False)
            gattr.cax.yaxis.set_label_position("right")
            gattr.cax.yaxis.set_major_formatter(FormatStrFormatter(params["formatbar"]) if params.get("formatbar") else ScalarFormatter())
        elif params.get("formatbar"):
            gattr.cax.yaxis.set_major_formatter(FormatStrFormatter(params["formatbar"]))
        if params.get("barlabel"):
            gattr.cax.set_ylabel(params["barlabel"], fontsize=float_param(params, "barlabelsz", context.label_style.fontsize),
                                 fontweight=normalize_fontweight(params.get("barlabelfat", context.label_style.fontweight)),
                                 color=context.axis_style.color or context.frame_style.color or "k")
    plt.tight_layout()
    movie = movie_request
    if movie not in (1, 2, 3) or context.output_format.lower() != "svg":
        movie = 0
    if sys.stdout.isatty():
        plt.show()
    elif movie:
        data_bin = data.reshape((data.n1, data.n2, data.n3, -1))
        nframes = data_bin.n(movie - 1)
        maxframe = int(float_param(params, "maxframe", 300))
        step = max(1, int(nframes / maxframe)) if nframes > maxframe else 1
        count = min(nframes, maxframe)
        axis = data_bin.axis(movie - 1)
        label = data_bin.label(movie - 1) or "Frame"
        unit = data_bin.unit(movie - 1)
        suffix = " (%s)" % unit if unit else ""
        out = io.StringIO()
        splitter = __SVG_SPLITTER[:-3] + 'framelabel="%s%s: %5g of %5g"' % (label, suffix, axis[0], axis[-1]) + __SVG_SPLITTER[-3:]
        out.write("\n%s\n" % splitter)
        save_figure(figure, out, "svg", dpi)
        updater = Grey3MovieUpdater()
        template = updater.build_template(out.getvalue(), movie, bool_param(params, "flat", True))
        vmin, vmax = gattr.im1.get_clim()
        state = MovieFrame(index=0, payload=data, clip=vmax - (vmin + vmax) / 2.0, bias=(vmin + vmax) / 2.0,
                           pclip=float_param(params, "pclip", 99.0), allpos=bool_param(params, "allpos", False),
                           cmap=cmap, dpi=dpi, frame1=frame1, frame2=frame2, frame3=frame3,
                           point1=float_param(params, "point1", 0.8), point2=float_param(params, "point2", 0.4), gain=gain)
        template = updater.update_frame(template, state)
        sys.stdout.write(template.svg)
        for iframe in range(1, count):
            state.frame1 = iframe * step if movie == 1 else frame1
            state.frame2 = iframe * step if movie == 2 else frame2
            state.frame3 = iframe * step if movie == 3 else frame3
            if gain_each:
                state.gain = estimate_gain(_movie_plane(data, movie, state.frame1, state.frame2, state.frame3),
                                           clip=float_param(params, "clip", None),
                                           pclip=float_param(params, "pclip", 99.0),
                                           bias=float_param(params, "bias", 0.0),
                                           mean=bool_param(params, "mean", False),
                                           allpos=bool_param(params, "allpos", False),
                                           gpow=float_param(params, "gpow", 1.0),
                                           polarity=bool_param(params, "polarity", False))
                state.clip, state.bias, state.allpos = state.gain.clip, state.gain.bias, state.gain.allpos
            template = updater.update_frame(template, state)
            index = iframe * step
            splitter = __SVG_SPLITTER[:-3] + 'framelabel="%s%s: %5g of %5g"' % (label, suffix, axis[index], axis[-1]) + __SVG_SPLITTER[-3:]
            sys.stdout.write("\n%s\n%s" % (splitter, template.svg))
            warning("Frame %d of %d;" % (iframe + 1, count))
        sys.stdout.flush()
        sys.stderr.write("\n")
    else:
        save_figure(figure, sys.stdout.buffer, context.output_format, dpi)
        sys.stdout.buffer.flush()
    plt.close(figure)
    return 0
