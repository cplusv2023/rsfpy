# -*- coding: utf-8 -*-
"""Shared command plumbing for RSFPY plotting tools.

This module deliberately contains command-line concerns only: parameter
compatibility, Matplotlib setup, common axes decoration, and saving figures.
The individual plot modules own how data is rendered.
"""

from dataclasses import dataclass, field
import logging
import os
import shutil
import subprocess
import sys
from typing import List, Optional

import matplotlib.pyplot as plt
import matplotlib.font_manager as font_manager
from matplotlib import use as use_backend
from matplotlib.ticker import FormatStrFormatter, MaxNLocator

from rsfpy.utils import _str_match_re
from rsfpy.version import __BASE_AX_NAME, __author__, __email__, __github__, __version__
from rsfpy.plot.parameters import canonical_params
from rsfpy.plot.style import element_style, normalize_fontweight
from .io import output_format, output_suffix


_COMMAND_DESCRIPTIONS = {
    "grey": "display RSF data as a grey or color image using matplotlib.",
    "graph": "display RSF data trace(s) as a 1-D graph image using matplotlib.",
    "wiggle": "display RSF data as a wiggle image using matplotlib.",
    "grey3": "display RSF data as a 3-D cube plot using matplotlib.",
}

_BOLD = "\033[1m"
_UNDERLINE = "\033[4m"
_RESET = "\033[0m"


def _heading(text):
    return "%s%s%s" % (_BOLD, text, _RESET)


def _parameter(kind, name, description):
    return "    %s%s%s\t%s%s%s\t%s" % (
        _UNDERLINE, kind, _RESET, _BOLD, name, _RESET, description)


_COMMON_PARAMETERS = (
    ("string", "backend=default", "Matplotlib backend; default lets Matplotlib choose."),
    ("string", "format=svg", "output format when stdout has no recognizable suffix."),
    ("float", "screenwidth/width=8.", "figure width in inches."),
    ("float", "screenheight/height=6.", "figure height in inches."),
    ("float", "dpi=100.", "figure resolution in dots per inch."),
    ("string", "bgcolor=w", "figure background color, including Matplotlib color names and #RRGGBB."),
    ("string", "facecolor=none", "axes background color; transparent by default."),
    ("string", "title=", "plot title; defaults to the RSF title header."),
    ("string", "label1= label2= label3=", "axis labels; defaults come from RSF headers."),
    ("string", "unit1= unit2= unit3=", "axis units; defaults come from RSF headers."),
    ("float", "min1= max1= min2= max2=", "display limits for the first two data axes."),
    ("bool", "transp=y", "transpose the first two display axes where supported."),
    ("bool", "xreverse=n yreverse=y", "reverse horizontal or vertical display direction."),
    ("string", "color/cmap=gray", "Matplotlib colormap; i, j, s map to gray, jet, seismic. A comma-separated color list creates a linear map."),
    ("float", "clip=", "symmetric display clip around bias."),
    ("float", "pclip=99.", "percentile used to estimate clip when clip is absent."),
    ("float", "bias=0.", "data value mapped to the center of the color table."),
    ("bool", "allpos=n", "map values from 0 to clip instead of using a symmetric range."),
    ("bool", "mean=n", "use the input mean as bias when estimating display gain."),
    ("float", "gpow=1.", "power-law display gain; 1 is linear."),
    ("bool", "polarity=n", "reverse the normalized color mapping."),
    ("string", "font/fontfamily=sans-serif", "font family name or a font file path."),
    ("float", "fontsz/fontsize=12.", "global font size; lower priority than label/title/tick sizes."),
    ("string", "fontfat/fontweight=normal", "global font weight: normal, bold, light, 700, and similar Matplotlib values."),
    ("float", "labelsz/labelsize=12.", "axis-label font size."),
    ("string", "labelfat/labelweight=normal", "axis-label font weight."),
    ("float", "titlesz/titlesize=14.", "title font size."),
    ("string", "titlefat/titleweight=bold", "title font weight."),
    ("float", "ticksz/ticksize=10.", "tick-label font size."),
    ("string", "tickfat/tickweight=normal", "tick-label font weight."),
    ("string", "framecolor=", "fallback color for frame, axes, grid, and overlays."),
    ("float", "framewidth/framefat=1.", "fallback line width for frame, axes, grid, and overlays."),
    ("string", "axiscolor=", "axis and spine color; falls back to framecolor."),
    ("float", "axiswidth/axisfat=", "axis and spine line width; falls back to framewidth."),
    ("bool", "grid=n", "draw major grid lines."),
    ("string", "gridcolor= gridstyle=--", "grid color and Matplotlib line style."),
    ("float", "gridwidth/gridfat=", "grid line width; falls back to axiswidth."),
    ("float", "ntic1/ntick1=5 ntic2/ntick2=5 ntic3/ntick3=", "maximum major tick counts."),
    ("string", "format1= format2= format3=", "printf-like tick format strings such as %.2f or %.3e."),
    ("string/float", "wheretitle=top", "title position: top, bottom, or a numeric axes coordinate."),
    ("string", "whereylabel=left wherexlabel=top", "axis-label placement."),
    ("string", "whereytick= wherextick=", "tick placement; defaults follow the corresponding label."),
    ("float", "maxpixels=4000000", "downsample image rasters above this pixel count for responsive rendering."),
)

_MODE_PARAMETERS = {
    "grey": (
        ("bool", "scalebar/colorbar=n", "draw a color bar."),
        ("string/file", "bar/barfile=", "byte-colorbar RSF file when input datatype is uchar and scalebar=y."),
        ("string", "barlabel/bartitle=", "color-bar label."),
        ("string", "barunit/unitbar=", "unit appended to the color-bar label."),
        ("string", "formatbar/barformat=", "color-bar tick format."),
        ("bool", "movie=y", "write one SVG frame per n3 panel when SVG is redirected."),
        ("int", "maxframe=300", "maximum number of movie frames; evenly subsamples larger sequences."),
        ("string", "gainpanel=0", "gain reference: a=all frames, e=each frame, or a one-based panel index."),
    ),
    "graph": (
        ("string", "lcolor/plotcol/linecolor=", "single trace color."),
        ("string", "lcolors=", "trace colors separated by commas, spaces, or semicolons."),
        ("string", "lstyle= lstyles=", "single or per-trace Matplotlib line styles."),
        ("float", "plotfat/linewidth=1.", "trace line width."),
        ("string", "marker/markers/symbol=", "marker style for traces."),
        ("float", "markersize/markersz=", "marker size."),
        ("bool", "stem=n", "draw a stem plot instead of connected traces."),
        ("bool", "logx=n logy=n", "use logarithmic horizontal or vertical axes."),
        ("float", "logxbase=10. logybase=10.", "logarithmic-axis bases."),
        ("bool", "movie=y", "write one SVG frame per n3 panel when SVG is redirected."),
        ("int", "maxframe=300", "maximum number of movie frames."),
    ),
    "wiggle": (
        ("float", "zplot=1.", "vertical wiggle exaggeration."),
        ("float", "clip= pclip=99. bias=0.", "wiggle amplitude clipping and bias."),
        ("bool", "fill=y", "enable positive and negative fills."),
        ("string", "pcolor= ncolor= lcolor=k", "positive fill, negative fill, and trace colors."),
        ("float", "plotfat/linewidth=1.", "trace line width."),
        ("string/file", "offset/xpos=", "optional RSF file containing trace positions."),
        ("bool", "movie=y", "write one SVG frame per n3 panel when SVG is redirected."),
        ("int", "maxframe=300", "maximum number of movie frames."),
    ),
    "grey3": (
        ("bool", "flat=y", "draw orthogonal slices as a flat layout; n draws the projected cube layout."),
        ("int", "frame1=0 frame2=0 frame3=0", "initial slice indices along the three data axes."),
        ("float", "point1=.8 point2=.4", "vertical and horizontal cube split positions."),
        ("bool", "scalebar/colorbar=n", "draw a color bar."),
        ("string/file", "bar/barfile=", "byte-colorbar RSF file when input datatype is uchar and scalebar=y."),
        ("string", "barlabel/bartitle= formatbar=", "color-bar label and tick format."),
        ("int", "movie=0", "movie axis: 1, 2, or 3; only SVG output supports movies."),
        ("int", "maxframe=300", "maximum number of movie frames."),
        ("string", "gainpanel=0", "gain reference: a=whole cube, e=each movie slice, or a one-based higher-dimensional panel."),
    ),
}


def command_documentation(mode):
    """Return detailed Madagascar-style terminal documentation for a plot mode."""

    program = "rsf%s" % mode
    description = _COMMAND_DESCRIPTIONS.get(mode, "display RSF data.")
    lines = [
        _heading("NAME"),
        "    %s" % program,
        _heading("DESCRIPTION"),
        "    %s" % description,
        _heading("SYNOPSIS"),
        "    %s < in.rsf [> out.svg] [key=value ...]" % program,
        _heading("COMMENTS"),
        "    Input data is read from stdin. If stdout is a terminal, the figure opens in a window.",
        "    Redirect stdout to create SVG, PNG, PDF, or another Matplotlib-supported format.",
        "    The output suffix selects the format; use format= when there is no useful suffix.",
        "    color/cmap accepts Matplotlib colormap names, RSFPY aliases i/j/s, or a comma-separated color list.",
        "    Parameters use Madagascar key=value syntax. Canonical names win when an alias is also supplied.",
        _heading("PARAMETERS"),
    ]
    lines.extend(_parameter(*item) for item in _COMMON_PARAMETERS)
    lines.append(_heading("PARAMETERS FOR %s" % program.upper()))
    lines.extend(_parameter(*item) for item in _MODE_PARAMETERS.get(mode, ()))
    lines.extend((
        _heading("PARAMETERS FOR EXTRA ELEMENTS"),
        "    Add up to ten numbered rectangles, arrows, or texts. Unnumbered style keys apply to all items.",
        _parameter("string", "rect#=min1,max1,min2,max2", "rectangle coordinates in data-axis order; # is 1 through 10."),
        _parameter("string", "rect#color= rect#facecolor=", "rectangle edge and fill colors; rectcolor and rectfacecolor are fallbacks."),
        _parameter("float", "rect#width= rect#alpha=", "rectangle line width and opacity; rectwidth and rectalpha are fallbacks."),
        _parameter("string", "rect#style=-", "rectangle Matplotlib line style; rectstyle is the fallback."),
        _parameter("string", "arrow#=y0,x0,dy,dx", "arrow tail and displacement in data coordinates."),
        _parameter("string", "arrow#color=", "arrow color; arrowcolor is the fallback."),
        _parameter("float", "arrow#width= arrow#alpha=", "arrow width and opacity; arrowwidth and arrowalpha are fallbacks."),
        _parameter("string", "text#=y,x,string", "text position and content; commas after the second one are kept in the text."),
        _parameter("string", "text#color= text#weight=", "text color and font weight; textcolor and textweight are fallbacks."),
        _parameter("float", "text#size= text#alpha=", "text font size and box opacity; textsize and textalpha are fallbacks."),
        _parameter("string", "text#facecolor= text#edgecolor=", "text-box colors; textfacecolor and textedgecolor are fallbacks."),
        _heading("MORE INFO"),
        "    Author: %s" % __author__,
        "    Email:  %s" % __email__,
        "    Source: %s" % __github__,
        _heading("VERSION"),
        "    %s" % __version__,
        "",
    ))
    return "\n".join(lines)


def wants_documentation(argv):
    """Recognize no-input documentation mode and explicit help switches."""

    if not argv:
        return sys.stdin.isatty()
    return any(arg in ("-h", "--help", "help=y", "help=Y") for arg in argv)


def show_documentation(mode):
    """Show help through the platform pager, falling back to terminal output."""

    text = command_documentation(mode)
    if sys.stdout.isatty() and shutil.which("less"):
        try:
            subprocess.run(["less", "-R"], input=text.encode(), check=False)
            return
        except OSError:
            pass
    print(text)


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
