# -*- coding: utf-8 -*-
"""Shared command plumbing for RSFPY plotting tools.

This module deliberately contains command-line concerns only: parameter
compatibility, Matplotlib setup, common axes decoration, and saving figures.
The individual plot modules own how data is rendered.
"""

from dataclasses import dataclass, field
import logging
import os
import sys
from typing import List, Optional

import matplotlib.pyplot as plt
import matplotlib.font_manager as font_manager
from matplotlib import use as use_backend
from matplotlib.ticker import FormatStrFormatter, MaxNLocator

from rsfpy.utils import _str_match_re
from rsfpy.version import __BASE_AX_NAME
from rsfpy.plot.parameters import canonical_params
from rsfpy.plot.style import element_style, normalize_fontweight
from .io import output_format, output_suffix


def bool_param(params, name, default=False):
    value = params.get(name)
    if value is None:
        return default
    return str(value).lower().startswith("y")


def float_param(params, name, default=None):
    value = params.get(name, default)
    if value in (None, ""):
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        warning("Warning: invalid %s=%s, use default %s." % (name, value, default))
        return default


def warning(*args, **kwargs):
    """Write a Madagascar-style warning without making stderr parsing brittle."""

    stream = kwargs.pop("file", sys.stderr)
    text = "".join(str(arg) for arg in args)
    if text.endswith(";"):
        default_end = "\r"
    elif text.endswith("."):
        default_end = "\n"
    else:
        default_end = ""
    end = kwargs.pop("end", default_end)
    progname = os.path.basename(sys.argv[0]) or "rsfpy"
    print("%s:" % progname, *args, file=stream, end=end, **kwargs)


def error(*args):
    warning(*args)
    raise SystemExit(1)


@dataclass
class PlotCommandContext:
    mode: str
    argv: List[str] = field(default_factory=lambda: sys.argv[1:])
    _params: Optional[dict] = field(default=None, init=False, repr=False)

    @property
    def params(self):
        if self._params is None:
            self._params = canonical_params(_str_match_re(self.argv))
        return self._params

    @property
    def frame_style(self):
        return element_style(self.params, "frame")

    @property
    def axis_style(self):
        return element_style(self.params, "axis", fallback_prefixes=("frame",))

    @property
    def grid_style(self):
        return element_style(self.params, "grid", fallback_prefixes=("axis", "frame"))

    @property
    def font_style(self):
        return element_style(self.params, "font", text=True)

    @property
    def label_style(self):
        return element_style(self.params, "label", fallback_prefixes=("font",), text=True)

    @property
    def title_style(self):
        return element_style(self.params, "title", fallback_prefixes=("font",), text=True)

    @property
    def output_suffix(self):
        return output_suffix()

    @property
    def output_format(self):
        return output_format(self.params, self.output_suffix)


def configure_matplotlib(context):
    """Apply shared backend/font settings before creating a figure."""

    params = context.params
    backend = params.get("backend", "default")
    if str(backend).lower() != "default":
        use_backend(backend)

    font = context.font_style
    family = font.fontfamily or "sans-serif"
    easy_names = {
        "times": ["Nimbus Roman", "Times New Roman"],
        "-1": ["Sans-serif"],
        "1": ["Sans-serif"],
        "2": ["Nimbus Roman", "Times New Roman", "DejaVu Sans"],
        "3": ["monospace", "DejaVu Sans"],
        "4": ["Noto Sans CJK", "DejaVu Sans"],
        "0": ["Arial", "DejaVu Sans"],
        "chinese": ["Noto Sans CJK", "SimSun", "SimHei", "Microsoft Yahei", "DejaVu Sans"],
    }
    if str(family).lower() in easy_names:
        families = easy_names[str(family).lower()]
    elif isinstance(family, str) and __import__("os").path.exists(family):
        font_manager.fontManager.addfont(family)
        families = [font_manager.FontProperties(fname=family).get_name()]
    else:
        families = [family]
    plt.rcParams["font.family"] = families + ["sans-serif"]

    tex_family = params.get("texfont", "3")
    tex_names = {
        "dejavusans": "dejavusans", "0": "dejavusans",
        "dejavuserig": "dejavuserig", "1": "dejavuserig",
        "cm": "cm", "2": "cm", "stix": "stix", "3": "stix",
        "stixsans": "stixsans", "4": "stixsans",
    }
    plt.rcParams["mathtext.fontset"] = tex_names.get(str(tex_family).lower(), "dejavusans")
    plt.rcParams["axes.unicode_minus"] = False
    plt.rcParams["svg.fonttype"] = "none"
    logging.getLogger("matplotlib.font_manager").setLevel(logging.ERROR)


def create_figure(context):
    params = context.params
    width = float_param(params, "screenwidth", 8.0)
    height = float_param(params, "screenheight", 6.0)
    dpi = float_param(params, "dpi", 100.0)
    figure = plt.figure(figsize=(width, height), dpi=dpi,
                         facecolor=params.get("bgcolor", "w"))
    axes = figure.add_subplot(1, 1, 1)
    axes.set_facecolor(params.get("facecolor", "none"))
    return figure, axes, dpi


def decorate_axes(context, axes, *, title=None, format1=None, format2=None,
                  ntic1=5, ntic2=5, colorbar=None, barlabel="", barunit=None,
                  barlabelsz=None, barlabelfat=None):
    """Apply common Madagascar-compatible frame, text, grid and overlay styles."""

    params = context.params
    frame = context.frame_style
    axis = context.axis_style
    grid = context.grid_style
    font = context.font_style
    label = context.label_style
    title_style = context.title_style
    frame_color = frame.color or "k"
    frame_width = frame.width if frame.width is not None else 1.0
    axis_width = axis.width if axis.width is not None else frame_width
    axis_color = axis.color or frame_color
    tick_size = float_param(params, "ticksz", font.fontsize or 12.0)
    tick_weight = normalize_fontweight(params.get("tickfat", params.get("tickweight", font.fontweight or "normal")))
    label_size = label.fontsize or font.fontsize or 12.0
    label_weight = label.fontweight or font.fontweight or "normal"
    title_size = title_style.fontsize or font.fontsize or 12.0
    title_weight = title_style.fontweight or font.fontweight or "normal"

    axes.tick_params(axis="both", which="major", labelsize=tick_size,
                     width=axis_width, colors=axis_color)
    if format1 is not None:
        try:
            axes.yaxis.set_major_formatter(FormatStrFormatter(format1))
        except (TypeError, ValueError):
            warning("Warning: invalid format1=%s, ignored." % format1)
    if format2 is not None:
        try:
            axes.xaxis.set_major_formatter(FormatStrFormatter(format2))
        except (TypeError, ValueError):
            warning("Warning: invalid format2=%s, ignored." % format2)
    axes.yaxis.set_major_locator(MaxNLocator(nbins=ntic1))
    axes.xaxis.set_major_locator(MaxNLocator(nbins=ntic2))

    ylabel = axes.yaxis.get_label().get_text()
    xlabel = axes.xaxis.get_label().get_text()
    axes.set_ylabel(ylabel, fontsize=label_size, fontweight=label_weight, color=label.color or axis_color)
    axes.set_xlabel(xlabel, fontsize=label_size, fontweight=label_weight, color=label.color or axis_color)

    if title:
        titleloc = params.get("wheretitle")
        if str(titleloc).lower() == "top":
            titleloc = 1.0
        elif str(titleloc).lower() == "bottom":
            titleloc = -0.08
        elif titleloc is not None:
            titleloc = float_param(params, "wheretitle", None)
        kwargs = {"fontsize": title_size, "fontweight": title_weight,
                  "color": title_style.color or axis_color}
        if titleloc is not None:
            kwargs["y"] = titleloc
        text = axes.set_title(title, **kwargs)
        text.set_gid("%s_title" % __BASE_AX_NAME)

    label1loc = str(params.get("whereylabel", "left")).lower()
    label2loc = str(params.get("wherexlabel", "top")).lower()
    tick1loc = str(params.get("whereytick", label1loc)).lower()
    tick2loc = str(params.get("wherextick", label2loc)).lower()
    if label1loc in ("left", "right"):
        axes.yaxis.set_label_position(label1loc)
    if label2loc in ("top", "bottom"):
        axes.xaxis.set_label_position(label2loc)
    if tick1loc in ("left", "right"):
        axes.yaxis.set_ticks_position(tick1loc)
    if tick2loc in ("top", "bottom"):
        axes.xaxis.set_ticks_position(tick2loc)
    for tick in axes.get_xticklabels() + axes.get_yticklabels():
        tick.set_fontweight(tick_weight)

    if bool_param(params, "grid", False):
        axes.grid(True, color=grid.color or axis_color,
                  linestyle=params.get("gridstyle", "--"),
                  linewidth=grid.width if grid.width is not None else axis_width,
                  alpha=0.5)
    for spine in axes.spines.values():
        spine.set_edgecolor(axis_color)
        spine.set_linewidth(axis_width)

    if colorbar is not None:
        colorbar.ax.tick_params(labelsize=tick_size, width=axis_width, colors=axis_color)
        for tick in colorbar.ax.get_yticklabels():
            tick.set_fontweight(tick_weight)
        if barlabel:
            colorbar.set_label(barlabel if barunit is None else "%s (%s)" % (barlabel, barunit),
                               fontsize=barlabelsz or label_size,
                               fontweight=barlabelfat or label_weight,
                               color=axis_color)
        for spine in colorbar.ax.spines.values():
            spine.set_edgecolor(axis_color)
            spine.set_linewidth(axis_width)


def add_overlays(context, axes):
    """Add the shared rect#/arrow#/text# annotations and an SVG bounding gid."""

    params = context.params
    frame = context.frame_style
    color = frame.color or "k"
    width = frame.width if frame.width is not None else 1.0
    font = context.font_style
    for index in range(1, 11):
        value = params.get("rect%d" % index)
        if value is None:
            continue
        try:
            min1, max1, min2, max2 = [float(item) for item in value.split(",")[:4]]
            patch = plt.Rectangle((min2, min1), max2 - min2, max1 - min1,
                                  fill=True,
                                  facecolor=params.get("rect%dfacecolor" % index, params.get("rectfacecolor", "none")),
                                  edgecolor=params.get("rect%dcolor" % index, params.get("rectcolor", color)),
                                  linewidth=float_param(params, "rect%dwidth" % index, float_param(params, "rectwidth", width)),
                                  linestyle=params.get("rect%dstyle" % index, params.get("rectstyle", "-")),
                                  alpha=float_param(params, "rect%dalpha" % index, float_param(params, "rectalpha", 1.0)))
            axes.add_patch(patch)
        except (TypeError, ValueError):
            warning("Warning: invalid rect%d=%s, ignored." % (index, value))
    for index in range(1, 11):
        value = params.get("arrow%d" % index)
        if value is None:
            continue
        try:
            y0, x0, dy, dx = [float(item) for item in value.split(",")[:4]]
            axes.add_patch(plt.Arrow(x0, y0, dx, dy,
                                     width=float_param(params, "arrow%dwidth" % index, float_param(params, "arrowwidth", 0.1)),
                                     color=params.get("arrow%dcolor" % index, params.get("arrowcolor", color)),
                                     alpha=float_param(params, "arrow%dalpha" % index, float_param(params, "arrowalpha", 1.0))))
        except (TypeError, ValueError):
            warning("Warning: invalid arrow%d=%s, ignored." % (index, value))
    for index in range(1, 11):
        value = params.get("text%d" % index)
        if value is None:
            continue
        try:
            y0, x0, text = value.split(",", 2)
            axes.text(float(x0), float(y0), text,
                      color=params.get("text%dcolor" % index, params.get("textcolor", color)),
                      fontsize=float_param(params, "text%dsize" % index, float_param(params, "textsize", font.fontsize or 12.0)),
                      fontweight=params.get("text%dweight" % index, params.get("textweight", font.fontweight or "normal")),
                      bbox={"facecolor": params.get("text%dfacecolor" % index, params.get("textfacecolor", "none")),
                            "edgecolor": params.get("text%dedgecolor" % index, params.get("textedgecolor", "none")),
                            "alpha": float_param(params, "text%dalpha" % index, float_param(params, "textalpha", 1.0))})
        except (TypeError, ValueError):
            warning("Warning: invalid text%d=%s, ignored." % (index, value))
    axes.set_gid(__BASE_AX_NAME)
    bbox = plt.Rectangle((0, 0), 1, 1, fill=False, edgecolor=(0, 0, 0, 0), transform=axes.transAxes)
    bbox.set_gid("%s_bbox" % __BASE_AX_NAME)
    axes.add_patch(bbox)


def save_figure(figure, output, output_format, dpi):
    figure.savefig(output, bbox_inches="tight", format=output_format, dpi=dpi, transparent=None)
