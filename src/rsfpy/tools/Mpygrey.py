#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
\033[1mNAME\033[0m
    \tMpygrey.py
\033[1mDESCRIPTION\033[0m
    \tdisplay RSF data as a grey or color image using matplotlib.
    \tsupport multiple modes:
    \t\033[1mmode=grey\033[0m\trsfgrey
    \t\033[1mmode=graph\033[0m\trsfgraph
    \t\033[1mmode=wiggle\033[0m\trsfwiggle
    \t\033[1mmode=grey3\033[0m\trsfgrey3
\033[1mSYNOPSIS\033[0m
    \tMpygrey.py < in.rsf [> out.[svg|pdf|png|jpg|...]] transp=y yreverse=y xreverse=n clip= pclip= allpos=n bias=0. verb=n scalebar=n color=gray bgcolor=w screenwidth=8. screenheight=6. dpi=100. label1= label2= title=
\033[1mCOMMENTS\033[0m
    \tInput data is from stdin, output figure is to stdout.

    \tIf output is not redirected to a file, the figure will be shown in a window (make sure you have graphic interface).

    \tcolor is a matplotlib colormap name, e.g., gray (or i), jet (or j), seismic (or s), hot, cool, etc. Explore: https://matplotlib.org/stable/users/explain/colors/colormaps.html#colormaps

    \tlcolor/pcolor/ncolor use simple color name, e.g., red (or r), blue (or b), black (or k),
    \t\t or RGB/RGBA (HEX) string like #abc (#aabbcc), #abcdefab, #0f0f0f, or none, etc. Explore: https://matplotlib.org/stable/users/explain/colors/colors.html

    \tfontfat is font weight, you can use numbers like 700, or: light, normal, medium, semibold, demibold, bold, heavy, ultralight, black, regular, book,  black
\033[1mPARAMETERS\033[0m
    \t\033[4mbool\033[0m\t\033[1mallpos=n\033[0m [y/n] if y, assume positive data
    \t\033[4mstring\033[0m\t\033[1mbarlabel/bartitle=\033[0m colorbar label
    \t\033[4mstring/file\033[0m\t\033[1mbar/barfile=\033[0m colorbar file (when datatype=uchar and scalebar=y)
    \t\033[4mstring\033[0m\t\033[1mbarlabelfat/barlabelweight=normal\033[0m colorbar label font weight: normal, bold, light, etc. (Can be numbers like 700)
    \t\033[4mfloat\033[0m\t\033[1mbarlabelsz/labelsize=12.\033[0m colorbar label font size (default 12)
    \t\033[4mstring\033[0m\t\033[1mbackend=default\033[0m matplotlib backend (default: let matplotlib decide)
    \t\033[4mfloat\033[0m\t\033[1mbgcolor/facecolor=w\033[0m background color (w: white, k: black)
    \t\033[4mstring\033[0m\t\033[1mbias=0.\033[0m value mapped to the center of the color table
    \t\033[4mfloat\033[0m\t\033[1mclip=\033[0m data clip
    \t\033[4mstring\033[0m\t\033[1mcolor/cmap=gray\033[0m color scheme (matplotlib colormap name, e.g., gray (i), jet (j), seismic (or s), hot, cool, etc)
    \t\033[4mfloat\033[0m\t\033[1mdpi=100.\033[0m figure resolution in dots per inch
    \t\033[4mbool\033[0m\t\033[1mflat=y\033[0m [y/n] if y, flatten the 3D data for grey3 plot
    \t\033[4mstring\033[0m\t\033[1mfont=sans-serif\033[0m font family
    \t\033[4mfloat\033[0m\t\033[1mfontsz/fontsize=12\033[0m global font size (low priority)
    \t\033[4mstring\033[0m\t\033[1mformat=svg\033[0m output figure format: pdf, png, jpg, etc (according to output suffix, default: svg)
    \t\033[4mstring\033[0m\t\033[1mformatbar/barformat=\033[0m format for colorbar tick labels, e.g., %.2f, %.3e, etc.
    \t\033[4mstring\033[0m\t\033[1mformat1=\033[0m format for axis 1 tick labels, e.g., %.2f, %.3e, etc.
    \t\033[4mstring\033[0m\t\033[1mformat2=\033[0m format for axis 2 tick labels, e.g., %.2f, %.3e, etc.
    \t\033[4mstring\033[0m\t\033[1mformat3=\033[0m format for axis 3 tick labels, e.g., %.2f, %.3e, etc. (Grey3 mode only)
    \t\033[4mint\033[0m\t\033[1mframe1=0\033[0m frame index along axis 1 (for grey3 plot)
    \t\033[4mint\033[0m\t\033[1mframe2=0\033[0m frame index along axis 2 (for grey3 plot)
    \t\033[4mint\033[0m\t\033[1mframe3=0\033[0m frame index along axis 3 (for grey3 plot)
    \t\033[4mstring\033[0m\t\033[1mframecolor=k\033[0m frame color
    \t\033[4mfloat\033[0m\t\033[1mframewidth=1.0\033[0m frame line width
    \t\033[4mbool\033[0m\t\033[1mgrid=n\033[0m [y/n] if y, show grid (Not working in grey3 mode)
    \t\033[4mstring\033[0m\t\033[1mgridstyle=--\033[0m grid line style
    \t\033[4mstring\033[0m\t\033[1mlabel1=\033[0m label for axis 1
    \t\033[4mstring\033[0m\t\033[1mlabel2=\033[0m label for axis 2
    \t\033[4mstring\033[0m\t\033[1mlabel3=\033[0m label for axis 3 (grey3 plot)
    \t\033[4mstring\033[0m\t\033[1mlabelfat/labelweight=normal\033[0m label font weight: normal, bold, light, etc. (Can be numbers like 700)
    \t\033[4mfloat\033[0m\t\033[1mlabelsz/labelsize=12.\033[0m label font size (default 12)
    \t\033[4mstring\033[0m\t\033[1mlcolor/plotcol/linecolor=\033[0m line color for graph plots
    \t\033[4mstring\033[0m\t\033[1mlcolors=\033[0m line colors for multiple traces (comma/space/semicolon separated, only for graph plots)
    \t\033[4mstring\033[0m\t\033[1mlstyles=\033[0m line styles for multiple traces (comma/space/semicolon separated, only for graph plots)
    \t\033[4mfloat\033[0m\t\033[1mlegendncol=1\033[0m number of columns in legend (only for graph plots)
    \t\033[4mfloat\033[0m\t\033[1mlegendalpha=0.8\033[0m legend transparency (only for graph plots)
    \t\033[4mbool\033[0m\t\033[1mlogx=n\033[0m [y/n] if y, use logarithmic scale for x-axis (only for graph plots)
    \t\033[4mbool\033[0m\t\033[1mlogy=n\033[0m [y/n] if y, use logarithmic scale for y-axis (only for graph plots)
    \t\033[4mfloat\033[0m\t\033[1mlogxbase=10.\033[0m base of logarithmic x-axis (only for graph plots)
    \t\033[4mfloat\033[0m\t\033[1mlogybase=10.\033[0m base of logarithmic y-axis (only for graph plots)
    \t\033[4mfloat\033[0m\t\033[1mmin1=\033[0m minimum value of axis 1 (overrides header d1 and o1)
    \t\033[4mfloat\033[0m\t\033[1mmin2=\033[0m minimum value of axis 2 (overrides header d2 and o2)
    \t\033[4mstring\033[0m\t\033[1mmax1=\033[0m maximum value of axis 1 (overrides header d1 and o1)
    \t\033[4mstring\033[0m\t\033[1mmax2=\033[0m maximum value of axis 2 (overrides header d2 and o2)
    \t\033[4mstring\033[0m\t\033[1mncolor=\033[0m negative value fill-in color for wiggle plot
    \t\033[4mfloat\033[0m\t\033[1mntic1/ntick1=5\033[0m max number of ticks on axis 1
    \t\033[4mfloat\033[0m\t\033[1mntic2/ntick2=5\033[0m max number of ticks on axis 2
    \t\033[4mfloat\033[0m\t\033[1mntic3/ntick3=\033[0m max number of ticks on axis 3
    \t\033[4mfloat\033[0m\t\033[1mnticbar/nticbar=\033[0m max number of ticks on colorbar
    \t\033[4mfloat\033[0m\t\033[1mpclip=99.\033[0m data clip percentile (default is 99)
    \t\033[4mbool\033[0m\t\033[1mscalebar/colorbar=n\033[0m [y/n] if y, draw scalebar
    \t\033[4mfloat\033[0m\t\033[1mscreenheight/height=6.\033[0m figure height in inches (default 6)
    \t\033[4mfloat\033[0m\t\033[1mscreenwidth/width=8.\033[0m figure width in inches (default 8)
    \t\033[4mstring\033[0m\t\033[1mtickfat/tickweight=normal\033[0m tick font weight: normal, bold, light, etc. (Can be numbers like 700)
    \t\033[4mfloat\033[0m\t\033[1mticksz/ticksize=10.\033[0m tick font size (default 12)
    \t\033[4mstring\033[0m\t\033[1mtitlefat/titleweight=bold\033[0m title font weight: normal, bold, light, etc. (Can be numbers like 700)
    \t\033[4mfloat\033[0m\t\033[1mtitlesz/titlesize=14.\033[0m title font size (default 12)
    \t\033[4mfloat\033[0m\t\033[1mplotfat/linewidth=1.0\033[0m line width for wiggle or graph plot
    \t\033[4mstring\033[0m\t\033[1mpcolor=\033[0m positive value fill-in color for wiggle plot
    \t\033[4mfloat\033[0m\t\033[1mpoint1=0.8\033[0m vertical aspect (for grey3 plot)
    \t\033[4mfloat\033[0m\t\033[1mpoint2=0.4\033[0m horizontal aspect (for grey3 plot)
    \t\033[4mbool\033[0m\t\033[1mtransp=y\033[0m [y/n] if y, transpose the display axes (Not working in grey3 plot)
    \t\033[4mstring\033[0m\t\033[1munit1=\033[0m unit for axis 1
    \t\033[4mstring\033[0m\t\033[1munit2=\033[0m unit for axis 2
    \t\033[4mstring\033[0m\t\033[1munit3=\033[0m unit for axis 3
    \t\033[4mbool\033[0m\t\033[1mverb=n\033[0m [y/n] verbosity flag
    \t\033[4mbool\033[0m\t\033[1mwheretitle=top\033[0m title position: top, bottom (Not working in grey3 plot)
    \t\033[4mstring\033[0m\t\033[1mwhereylabel=left\033[0m axis 1 label position: left, right (Not working in grey3 plot)
    \t\033[4mstring\033[0m\t\033[1mwherexlabel=bottom\033[0m axis 2 label position: top, bottom (Not working in grey3 plot)
    \t\033[4mstring\033[0m\t\033[1mwherextick=bottom\033[0m horizontal axis tick position: top, bottom (Not working in grey3 plot)
    \t\033[4mstring\033[0m\t\033[1mwhereytick=left\033[0m vertical axis tick position: left, right (Not working in grey3 plot)
    \t\033[4mbool\033[0m\t\033[1mxreverse=n\033[0m [y/n] if y, reverse the horizontal axis (Not working in grey3 plot)
    \t\033[4mbool\033[0m\t\033[1myreverse=y\033[0m [y/n] if y, reverse the vertical axis (Not working in grey3 plot)
    \t\033[4mfloat\033[0m\t\033[1mzplot=1.0\033[0m vertical exaggeration factor for wiggle plot

\033[1mPARAMETERS FOR EXTRA ELEMENTS\033[0m
    \tYou can add rectangles, arrows, and texts to the figure using parameters below. At most 10 rectangles, arrows, and texts can be added.

    \tParameters with no # are common for all rectangles/arrows/texts.

    \tFor example, retc1=0.1,0.5,0.2,0.6 rect2=1.1,1.5,1.2,1.6 rect1color=red rectwidth=2.0 rect2style=-- 
    \twill draw two rectangles, the first one with red solid line of width 2.0, the second one with black dashed line of width 1.0.

    \t\033[4mstring\033[0m\t\033[1mrect#=min1,max1,min2,max2\033[0m rectangle #=1,2,... (up to rect10)
    \t\033[4mstring\033[0m\t\033[1mrect#color=k\033[0m rectangle color (default is framecolor)
    \t\033[4mfloat\033[0m\t\033[1mrect#width=1.0\033[0m rectangle line width (default is framewidth)
    \t\033[4mstring\033[0m\t\033[1mrect#style=-\033[0m rectangle line style (default is -)
    \t\033[4mfloat\033[0m\t\033[1mrect#alpha=1.0\033[0m rectangle line alpha (default is 1.0)
    \t\033[4mstring\033[0m\t\033[1marrow#=y0,x0,dy,dx\033[0m arrow #=1,2,... (up to arrow10), (y0,x0) is the tail position, (dy,dx) is the displacement
    \t\033[4mstring\033[0m\t\033[1marrow#color=k\033[0m arrow color (default is framecolor)
    \t\033[4mfloat\033[0m\t\033[1marrow#width=0.1\033[0m arrow line width (default is 0.1)
    \t\033[4mfloat\033[0m\t\033[1marrow#alpha=1.0\033[0m arrow line alpha (default is 1.0)
    \t\033[4mstring\033[0m\t\033[1mtext#=x,y,string\033[0m text #=1,2,... (up to text10), (x,y) is the position, string is the text content
    \t\033[4mstring\033[0m\t\033[1mtext#color=k\033[0m text color (default is framecolor)
    \t\033[4mfloat\033[0m\t\033[1mtext#size=12.\033[0m text font size (default is fontsz)
    \t\033[4mstring\033[0m\t\033[1mtext#weight=normal\033[0m text font weight: normal, bold, light, etc. (Can be numbers like 700) (default is fontfat)
    \t\033[4mstring\033[0m\t\033[1mtext#facecolor=none\033[0m text box face color (default is none)
    \t\033[4mstring\033[0m\t\033[1mtext#edgecolor=none\033[0m text box edge color (default is none)
    \t\033[4mfloat\033[0m\t\033[1mtext#alpha=0.5\033[0m text box alpha (default is 0.5)

\033[1mMORE INFO\033[0m
    \tAuthor:\tauthor_label
    \tEmail:\temail_label
    \tSource:\tgithub_label
\033[1mVERSION\033[0m
    \tversion_label
"""



import matplotlib.pyplot as plt
import matplotlib.cm as cm
import matplotlib.colors as mcolors
import matplotlib.font_manager as font_manager
import numpy as np
from matplotlib import use as use_backend
from matplotlib.ticker import MaxNLocator, FormatStrFormatter, LogLocator, FuncFormatter, ScalarFormatter
import sys, subprocess, os, re
from textwrap import dedent

from rsfpy import Rsfarray
from rsfpy.utils import _str_match_re, _get_stdname
from rsfpy.version import __version__, __email__, __author__, __github__, __SVG_SPLITTER

__progname__ = os.path.basename(sys.argv[0])
DESCRIPTION = {
    "rsfgrey": "display RSF data as a grey or color image using matplotlib.",
    "rsfgraph": "display RSF data trace(s) as a 1-D graph image using matplotlib.",
    "rsfwiggle": "display RSF data as a wiggle image using matplotlib.",
    "rsfgrey3": "display RSF data as a 3-D cube plot using matplotlib.",
}
__description__ = DESCRIPTION.get(__progname__, DESCRIPTION["rsfgrey"])
__doc__ = __doc__.replace(DESCRIPTION["rsfgrey"],DESCRIPTION[__progname__])
__doc__ = __doc__.replace("author_label",__author__)
__doc__ = __doc__.replace("email_label",__email__)
__doc__ = __doc__.replace("github_label",__github__)
__doc__ = __doc__.replace("version_label",__version__)

DOC = dedent(__doc__.replace('Mpygrey.py', __progname__))

def main():
    if len(sys.argv) < 2 and sys.stdin.isatty():
        subprocess.run(['less', '-R'], input=DOC.encode())
        sys.exit(1)
    par_dict = _str_match_re(sys.argv[1:])

    stdname = _get_stdname()
    if stdname[1] is not None: suffix = os.path.splitext(stdname[1])[1].lower()
    else:suffix = '.svg'
    if suffix not in [
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
        ".pgf"
    ]: suffix = ".svg"
    # Check stdin
    if sys.stdin.isatty():
        sf_error("Error: no input data?")

    # Read data
    data = Rsfarray(sys.stdin.buffer)
    if data.size == 0:
        sf_error("Failed read RSF data from input.")
    datatype = data.dtype
    if datatype not in [np.int32, np.float32, np.complex64, np.uint8]:
        sf_error(f"Error: unsupported data type: {datatype} ?")

    
    # Get parameters
    backend = par_dict.get('backend', 'default')
    if backend.lower() != 'default': use_backend(backend)
    transp = par_dict.get('transp', 'y').lower().startswith('y')
    yreverse = par_dict.get('yreverse', 'y').lower().startswith('y')
    xreverse = par_dict.get('xreverse', 'n').lower().startswith('y')
    allpos = par_dict.get('allpos', 'n').lower().startswith('y')
    verb = par_dict.get('verb', 'n').lower().startswith('y')
    scalebar = par_dict.get('scalebar',
                            par_dict.get('colorbar', 'n')
                            ).lower().startswith('y')
    color = par_dict.get('color', par_dict.get('cmap', 'gray'))
    label1 = par_dict.get('label1', data.label1)
    label2 = par_dict.get('label2', data.label2)
    unit1 = par_dict.get('unit1', data.unit1)
    unit2 = par_dict.get('unit2', data.unit2)
    barunit = par_dict.get('barunit', par_dict.get('unitbar', None))
    title = par_dict.get('title', data.header.get('title', ''))
    clip = getfloat(par_dict, 'clip', None)
    pclip = getfloat(par_dict, 'pclip', 99.)
    bias = getfloat(par_dict, 'bias', 0.)
    fig_width = getfloat(par_dict, 'screenwidth',
                         getfloat(par_dict, 'width', 8.))
    fig_height = getfloat(par_dict, 'screenheight',
                          getfloat(par_dict, 'height', 6.))
    facecolor = par_dict.get('bgcolor', par_dict.get('facecolor', 'w'))
    dpi = getfloat(par_dict, 'dpi', 100.)
    fontsz = getfloat(par_dict, 'fontsz',
                      getfloat(par_dict, 'fontsize', 12.))
    font_family = par_dict.get('font', 'sans-serif')
    fontweight = par_dict.get('fontfat', par_dict.get('fontweight', 'normal'))
    labelsz = getfloat(par_dict, 'labelsz',
                       getfloat(par_dict, 'labelsize', fontsz)
                       )

    titlesz = getfloat(par_dict, 'titlesz',
                       getfloat(par_dict, 'titlesize', fontsz))
    ticksz = getfloat(par_dict, 'ticksz', fontsz)
    labelfat = par_dict.get('labelfat', par_dict.get('labelweight', fontweight))
    titlefat = par_dict.get('titlefat', par_dict.get('titleweight', fontweight))
    tickfat = par_dict.get('tickfat', par_dict.get('tickweight', fontweight))
    gridon = par_dict.get('grid', 'n').lower().startswith('y')
    gridstyle = par_dict.get('gridstyle', '--')
    frame_color = par_dict.get('framecolor', 'k')
    frame_width = getfloat(par_dict, 'framewidth', 1.0)
    ntic1 = getfloat(par_dict, 'ntic1',
                     getfloat(par_dict, 'ntick1', 5))
    ntic2 = getfloat(par_dict, 'ntic2',
                     getfloat(par_dict, 'ntick2', 5))
    nticbar = getfloat(par_dict, 'nticbar',
                          getfloat(par_dict, 'ntickbar', 5))
    min1 = getfloat(par_dict, 'min1', None)
    min2 = getfloat(par_dict, 'min2', None)
    max1 = getfloat(par_dict, 'max1', None)
    max2 = getfloat(par_dict, 'max2', None)
    maxval = getfloat(par_dict, 'maxval', None)
    minval = getfloat(par_dict, 'minval', None)
    label1loc = par_dict.get('whereylabel', 'left').lower()
    label2loc = par_dict.get('wherexlabel', 'top').lower()
    tick1loc = par_dict.get('whereytick', label1loc).lower()
    tick2loc = par_dict.get('wherextick', label2loc).lower()
    titleloc = getfloat(par_dict, 'wheretitle', None)
    format1 = par_dict.get('format1', None)
    format2 = par_dict.get('format2', None)
    formatbar = par_dict.get('formatbar', par_dict.get('barformat', None))
    barlabel = par_dict.get('barlabel', par_dict.get('bartitle', ''))
    barlabelfat = par_dict.get('barlabelfat', par_dict.get('barlabelweight', fontweight))
    barlabelsz = getfloat(par_dict, 'barlabelsz',
                          getfloat(par_dict, 'barlabelsize', fontsz))
    pformat = par_dict.get('format', suffix[1:])
    movie = par_dict.get('movie', 'n').endswith('y')
    maxframe = int(getfloat(par_dict, 'maxframe', 30))

    # Check plot type
    plottype = par_dict.get('plottype', 'grey').lower()
    # command line support
    if sys.argv[0].endswith('grey'):
        plottype = 'grey'
    elif sys.argv[0].endswith('wiggle'):
        plottype = 'wiggle'
    elif sys.argv[0].endswith('graph'):
        plottype = 'graph'
    elif sys.argv[0].endswith('grey3'):
        plottype = 'grey3'

    if plottype == 'wiggle':
        if datatype == np.uint8:
            sf_error("Error: wiggle plot does not support uchar data.")
        # Parameters for wiggle plot
        zplot = getfloat(par_dict, 'zplot', 1.0)
        lcolor = par_dict.get('lcolor', par_dict.get('plotcol', par_dict.get('linecolor', 'k')))
        ncolor = par_dict.get('ncolor', 'none')
        pcolor = par_dict.get('pcolor', lcolor)
        if par_dict.get('fill', 'y').lower().startswith('n'):
            ncolor = 'none'
            pcolor = 'none'
        plotfat = getfloat(par_dict, 'linewidth', 
                           getfloat(par_dict, 'plotfat', frame_width))
    elif plottype == 'graph':
        if datatype == np.uint8:
            sf_error("Error: wiggle plot does not support uchar data.")
        # Parameters for graph plot
        transp = par_dict.get('transp', 'n').lower().startswith('y')
        yreverse = par_dict.get('yreverse', 'n').lower().startswith('y')
        lcolor = par_dict.get('lcolor', par_dict.get('plotcol', par_dict.get('linecolor', 'k')))
        lcolors = par_dict.get('lcolors', None)
        lstyle = par_dict.get('lstyle', par_dict.get('linestyle', None))
        lstyles = par_dict.get('lstyles', par_dict.get('linestyles', None))
        label2loc = par_dict.get('wherexlabel', 'bottom').lower()
        tick2loc = par_dict.get('wherextick', label2loc).lower()
        marker = par_dict.get('marker',
                              par_dict.get('symbol', None))
        markers = par_dict.get('markers',
                               par_dict.get('symbols', None))
        markersize = getfloat(par_dict, 'markersize',
                              getfloat(par_dict, 'markersz',
                                       getfloat(par_dict, 'symbolsize',
                                                getfloat(par_dict, 'symbolsz', fontsz))))
        stem = par_dict.get('stem', 'n').lower().startswith('y')
        plotfat = getfloat(par_dict, 'linewidth', 
                           getfloat(par_dict, 'plotfat', frame_width))
        legendon = par_dict.get('legend', 'n').lower().startswith('y')
        legends = par_dict.get('legends', None)
        wherelegend = par_dict.get('wherelegend', 'best')
        legendbox = par_dict.get('legendbox', 'n').lower().startswith('y')
        legendsize = getfloat(par_dict, 'legendsize', fontsz)
        legendfat = par_dict.get('legendfat', fontweight)
        legendalpha = getfloat(par_dict, 'legendalpha', 0.8)
        legendncol = getfloat(par_dict, 'legendncol', 1)
        logx = par_dict.get('logx', 'n').lower().startswith('y')
        logy = par_dict.get('logy', 'n').lower().startswith('y')
        logxbase = getfloat(par_dict, 'logxbase', 10.)
        logybase = getfloat(par_dict, 'logybase', 10.)

        # Unpack legends
        if legendon:
            if legends is not None:
                legends = re.split(r'[ ,;]+', legends)
                if len(legends) != data.n2:
                    sf_warning(f"Warning: number of legends {len(legends)} != number of traces {data.n2}, ignore legends.")
                    legends = None
            if legends is None:
                if data.ndim > 1: legends = [f'{data.label2}={ia}' for ia in data.axis2]
                else: legends = None
        else:
            legends = None
        # Unpack lcolors
        if lcolors is not None:
            lcolors = re.split(r'[ ,;]+', lcolors)
            while len(lcolors) < data.n2:
                lcolors.append(lcolor)
        elif lcolor is not None:
            lcolors = []
            while len(lcolors) < data.n2:
                lcolors.append(lcolor)
        else:
            lcolors = ['k', 'r', 'g', 'b', 'c', 'm', 'y']
            while len(lcolors) < data.n2:
                lcolors += lcolors
            lcolors = lcolors[:data.n2]

        # Unpack lstyles
        if lstyles is not None:
            lstyles = re.split(r'[ ,;]+', lstyles)
            while len(lstyles) < data.n2:
                lstyles.append('-')
        elif lstyle is not None:
            lstyles = []
            while len(lstyles) < data.n2:
                lstyles.append(lstyle)
        else:
            lstyles = ['-','--','-.',':']
            while len(lstyles) < data.n2:
                lstyles += lstyles
            lstyles = lstyles[:data.n2]

        if markers is not None:
            markers = re.split(r'[ ,;]+', markers)
            while len(markers) < data.n2:
                markers.append('.')
        elif marker is not None:
            markers = []
            while len(markers) < data.n2:
                markers.append(marker)
        elif stem:
            markers = ['.',',','o','*','s','p','x','d','v','^','<','>']
            while len(markers) < data.n2:
                markers += markers
            markers = markers[:data.n2]
        else:
            markers = ['none'] * data.n2
        # scalebar = False
    elif plottype == 'grey3':
        frame1 = int(getfloat(par_dict, 'frame1', 0.0))
        frame2 = int(getfloat(par_dict, 'frame2', 0.0))
        frame3 = int(getfloat(par_dict, 'frame3', 0.0))
        if frame1 > data.n1: frame1 = data.n1 - 1
        elif frame1 < 0: frame1 = 0
        if frame2 > data.n2: frame2 = data.n2 - 1
        elif frame2 < 0: frame2 = 0
        if frame3 > data.n3: frame3 = data.n3 - 1
        elif frame3 < 0: frame3 = 0
        point1 = getfloat(par_dict, 'point1', 0.8)
        point2 = getfloat(par_dict, 'point2', 0.4)
        isflat = par_dict.get('flat', 'y').lower().startswith('y')
        label3 = par_dict.get('label3', data.label3)
        unit3 = par_dict.get('unit3', data.unit3)
        format3 = par_dict.get('format3', None)
        ntic3 = getfloat(par_dict, 'ntic3',
                         getfloat(par_dict, 'ntick3', None))

    if datatype == np.uint8 and scalebar:
        # Process bar
        barfile = par_dict.get('barfile', par_dict.get('bar', None))
        try:
            bar_arrays = Rsfarray(barfile)
            if plottype=='grey3':bar_array = bar_arrays.window(n3=1, f3=frame3)
            else:bar_array = bar_arrays.window(n3=1)
            min_max_vals = np.frombuffer(bar_array[:8].tobytes(), dtype=np.float32)
            min_val, max_val = min_max_vals[0], min_max_vals[1]
        except Exception as e:
            sf_error(f"Error reading bar= when scalebar=y, {e}")
    if datatype == np.int32:
        sf_warning(f"Got {datatype}, converting to float32.")
        data = Rsfarray(data.astype(np.float32), header=data.header)
    if datatype == np.complex64 and plottype!= 'graph':
        sf_warning(f"Got {datatype}, converting to float32 using abs.")
        data = Rsfarray(np.abs(data), header=data.header)



    # Verbose Message
    if verb: sf_warning(par_dict, '.')

    # Colormap
    cmapper = {
        'j':'jet',
        'J':'jet_r',
        'i':'gray',
        'I':'gray',
        's':'seismic',
        'S':'seismic_r',
    }
    if color in cmapper.keys():
        color = cmapper[color]

    data.sfput(label1=label1, unit1=unit1)
    data.sfput(label2=label2, unit2=unit2)

    # Check some parameters could cause errors
    fontfats = ['light', 'normal', 'medium', 'semibold', 'demibold', 'bold', 'heavy', 'ultralight', 'black', 'regular', 'book',  'black']
    if fontweight not in fontfats:
        try: fontweight = float(fontweight)
        except:
            sf_warning(f"Warning: invalid fontfat={fontweight}, use default normal.")
        fontweight = 'normal'
    if labelfat not in fontfats:
        try: labelfat = float(labelfat)
        except:
            sf_warning(f"Warning: invalid labelfat={labelfat}, use default {fontweight}.")
            labelfat = fontweight
    if titlefat not in fontfats and not titlefat.isdigit():
        try: titlefat = float(titlefat)
        except:
            sf_warning(f"Warning: invalid titlefat={titlefat}, use default {fontweight}.")
            titlefat = fontweight
    if tickfat not in fontfats:
        try: tickfat = float(tickfat)
        except:
            sf_warning(f"Warning: invalid titlefat={tickfat}, use default {fontweight}.")
            tickfat = fontweight
    if barlabelfat not in fontfats:
        try: barlabelfat = float(barlabelfat)
        except:
            sf_warning(f"Warning: invalid barlabelfat={barlabelfat}, use default {fontweight}.")
            barlabelfat = fontweight

    easy_font_name = {
        'Times': 'Nimbus Roman', #Embrace open source
        '-1': 'Sans-serif',
        '1': 'Sans-serif',
        '2': 'Nimbus Roman',
        '3': 'Courier New',
        '4': 'Noto Sans CJK',
        '0': 'Arial',
        'Chinese': 'Noto Sans CJK',
    }
    if font_family in easy_font_name.keys():
        plt.rcParams['font.family'] = [easy_font_name.get(font_family, 'Sans-serif'), 'Sans-serif']
    elif os.path.exists(font_family):
        font_manager.fontManager.addfont(font_family)
        new_font = font_manager.FontProperties(fname=font_family)
        plt.rcParams['font.family'] = [new_font.get_name(), 'Sans-serif']
    else: plt.rcParams['font.family'] = [font_family, 'Sans-serif']
    plt.rcParams['axes.unicode_minus'] = False

    ################################################################################
    # Support multiple frames
    if plottype == 'grey3':
        movie = int(getfloat(par_dict, 'movie', 0))
    databin = None
    nframes = 1
    frame_step = 1
    frame_axis = None
    frame_prefix = "Frame"
    frame_suffix = ""
    if pformat.lower() != 'svg':
        movie = False
    elif movie:
        databin = data
        dpi = getfloat(par_dict, 'dpi', 50)

        # determine number of frames
        if plottype == 'grey':
            databin = data.reshape((data.n1, data.n2, -1))
            nframes = databin.n3
            if nframes > maxframe:
                frame_step = int((nframes / maxframe))
                sf_warning(f"Got {nframes} movie frames, while maxframe is {maxframe}, use framestep {frame_step}.")
            frame_axis = databin.axis3
            frame_prefix = databin.label3 if databin.label3 is not None else "Frame"
            frame_suffix = f" ({databin.unit3})" if databin.unit3 is not None else ""
            if nframes == 1:
                movie = False
        if plottype == 'grey3':
            if movie == 0: movie = False
            elif movie < 4:
                databin = data.reshape((data.n1, data.n2, data.n3, -1))
                nframes = databin.n(movie - 1)
                if nframes > maxframe:
                    frame_step = int((nframes / maxframe))
                    sf_warning(f"Got {nframes} movie frames, while maxframe is {maxframe}, use framestep {frame_step}.")
                frame_axis = databin.axis(movie-1)
                frame_prefix = databin.label(movie-1) if databin.label(movie-1) is not None else "Frame"
                frame_suffix = f" ({databin.unit(movie-1)})" if databin.unit(movie-1) is not None else ""
                if nframes == 1:
                    movie = False

    if not movie: maxframe = 1

    fig = plt.figure(figsize=(fig_width, fig_height), dpi=dpi, facecolor=facecolor)
    ax = fig.add_subplot(1, 1, 1)
    cbar = None
    for iframe in range(min(nframes, maxframe)):
        if plottype == 'grey':
            if movie: data = databin.window(n3=1, f3=iframe*frame_step)

            if iframe==0:
                data.grey(ax=ax, transp=transp, yreverse=yreverse, xreverse=xreverse,
                    allpos=allpos, clip=clip, pclip=pclip, bias=bias, cmap=color,
                    min1=min1, max1=max1, min2=min2, max2=max2,
                    colorbar=False, show=False, interpolation="none")
            else:
                ax.images[0].set_data(data)
                newbar = bar_arrays.window(n3=1, f3=iframe*frame_step)
                new_min_max_vals = np.frombuffer(newbar[:8].tobytes(), dtype=np.float32)
                new_min, new_max = tuple(new_min_max_vals)
                cbar.ax.images[0].set_extent([0, 1, new_min, new_max])
                cbar.ax.yaxis.set_major_formatter(
                    FormatStrFormatter(formatbar) if formatbar is not None
                    else ScalarFormatter())
                cbar.ax.yaxis.set_major_locator(MaxNLocator(nbins=nticbar))

                cbar.ax.images[0].set_data(255 - newbar[8:, np.newaxis])

                sf_warning(f"Frame {iframe + 1} of {min(nframes, maxframe)};")
                # save or show figure
                if not sys.stdout.isatty():
                    if movie:
                        splitter = (__SVG_SPLITTER[:-3] +
                                f"framelabel=\"{frame_prefix}{frame_suffix}: "+
                                f"{frame_axis[iframe*frame_step]:5g} of {frame_axis[-1]:5g}\"" +
                                __SVG_SPLITTER[-3:])
                        sys.stdout.write(f"\n{splitter}\n")
                        sys.stdout.flush()
                    fig.savefig(sys.stdout.buffer, bbox_inches='tight', format=pformat, dpi=dpi,
                                transparent=None)
                    sys.stdout.flush()
                else:
                    plt.pause(0.01)
                continue
        elif plottype == 'wiggle':
            data.wiggle(ax=ax,
                    transp=transp, yreverse=yreverse, xreverse=xreverse,
                    min1=min1, max1=max1, min2=min2, max2=max2,
                    zplot=zplot, bias=bias, clip=clip, pclip=pclip,
                    ncolor=ncolor, pcolor=pcolor, lcolor=lcolor,
                    linewidth=plotfat,
                    show=False)
        elif plottype == 'graph':
            if data.ndim < 2:
                data = data.reshape((data.n1, 1))
            for itrace in range(data.n2):
                if transp:
                    if datatype == np.complex64:
                        y, x = np.real(data[:, itrace]), np.imag(data[:, itrace])
                    else:
                        x, y = data[:, itrace].squeeze(), data.axis1

                else:
                    if datatype == np.complex64:
                        x, y = np.real(data[:, itrace]), np.imag(data[:, itrace])
                    else:
                        y, x = data[:, itrace].squeeze(), data.axis1
                if stem:
                    stemmarker = markers[itrace] if markers else 'o'
                    stemlabel = legends[itrace] if legends else None
                    stemcolor = lcolors[itrace]

                    # stem 返回 StemContainer，可以单独设置 stemline, markerline, baseline
                    stem_container = ax.stem(
                        x, y,
                        linefmt=stemcolor + '-',  # 茎的线条样式
                        markerfmt=stemcolor + stemmarker,  # 顶端 marker 样式
                        basefmt=frame_color+lstyles[itrace],  # 基线样式
                        label=stemlabel
                    )
                    stem_container.markerline.set_markersize(markersize)
                    stem_container.stemlines.set_linewidth(plotfat)
                else:
                    ax.plot(x, y, color=lcolors[itrace], linewidth=plotfat,
                            linestyle=lstyles[itrace],
                            label=legends[itrace] if legends else None,
                            marker=markers[itrace], markersize=markersize)

            amin1, amax1 = ax.get_xlim()
            amin2, amax2 = ax.get_ylim()
            min1 = min1 if min1 is not None else amin1
            max1 = max1 if max1 is not None else amax1
            min2 = min2 if min2 is not None else amin2
            max2 = max2 if max2 is not None else amax2

            if logx and logxbase <= 0.:
                sf_warning(f"Warning: invalid logxbase={logxbase}, use default 10.")
                logxbase = 10.
            if logy and logybase <= 0.:
                sf_warning(f"Warning: invalid logybase={logybase}, use default 10.")
                logybase = 10.
            if logx and min1 <= 0.:
                sf_warning(f"Warning: logx but min2={min1} <= 0, use normal x axis.")
                logx = False
            if logy and min2 <= 0.:
                sf_warning(f"Warning: logy but min1={min2} <= 0, use normal y axis.")
                logy = False

            if logx:
                ax.set_xscale('log', base=logxbase)
                ax.xaxis.set_major_locator(LogLocator(base=logxbase, numticks=ntic2))
            if logy:
                ax.set_yscale('log', base=logybase)
                ax.yaxis.set_major_locator(LogLocator(base=logybase, numticks=ntic1))
            if xreverse:
                ax.set_xlim(max1, min1)
            else:
                ax.set_xlim(min1, max1)
            if yreverse:
                ax.set_ylim(max2, min2)
            else:
                ax.set_ylim(min2, max2)
            if legendon:
                legend = ax.legend(loc=wherelegend, fontsize=legendsize,
                                frameon=legendbox, ncol=legendncol)
                for text in legend.get_texts():
                    text.set_fontweight(legendfat)
                if legendbox:
                    legend.get_frame().set_alpha(legendalpha)
            if not transp:
                ax.set_xlabel(data.label_unit(0))
                ax.set_ylabel(data.label_unit(1))
            else:
                ax.set_ylabel(data.label_unit(0))
                ax.set_xlabel(data.label_unit(1))


        elif plottype == 'grey3':
            if movie == 1: frame1 = iframe*frame_step
            if movie == 2: frame2 = iframe*frame_step
            if movie == 3: frame3 = iframe*frame_step
            if iframe ==0:
                data.sfput(label3=label3, unit3=unit3)

                gattr = data.grey3(ax=ax, frame1=frame1, frame2=frame2, frame3=frame3,
                           point1=point1, point2=point2, colorbar=scalebar, cmap=color,
                           clip=clip, pclip=pclip,bias=bias, allpos=allpos,
                                   title=title, n3tic=ntic3, ntic1=ntic1, ntic2=ntic2,
                                   format1=format1, format2=format2, format3=format3,
                           flat=isflat, show=False)
                gattr.set_title(title, fontsize=titlesz, fontweight=titlefat, color=frame_color)
                gattr.set_lines(color=frame_color,width=frame_width)
                gattr.set_spines(color=frame_color,width=frame_width)
                gattr.set_ticklabels(color=frame_color,fontsize=ticksz, fontweight=tickfat)
                gattr.set_labels(color=frame_color,fontsize=labelsz, fontweight=labelfat)
                if format1 is not None: gattr.ax1.yaxis.set_major_formatter(FormatStrFormatter(format1))
                if format2 is not None: gattr.ax1.xaxis.set_major_formatter(FormatStrFormatter(format2))
                if format3 is not None:
                    gattr.ax2.xaxis.set_major_formatter(FormatStrFormatter(format3))
                    gattr.ax3.yaxis.set_major_formatter(FormatStrFormatter(format3))
                if ntic1 is not None: gattr.ax1.yaxis.set_major_locator(MaxNLocator(nbins=ntic1))
                if ntic2 is not None: gattr.ax1.xaxis.set_major_locator(MaxNLocator(nbins=ntic2))
                if ntic3 is not None: gattr.ax2.xaxis.set_major_locator(MaxNLocator(nbins=ntic3))
                if ntic3 is not None: gattr.ax3.yaxis.set_major_locator(MaxNLocator(nbins=ntic3))

                if scalebar:
                    if datatype == np.uint8:
                        # Overlap existing colorbar
                        gattr.cax.clear()
                        gattr.cax.imshow(255 - bar_array[8:, np.newaxis], aspect='auto', cmap=color,
                                         vmin=0, vmax=255, extent=[0,1,min_val,max_val])
                        gattr.cax.xaxis.set_visible(False)
                        gattr.cax.yaxis.set_label_position('right')
                        gattr.cax.yaxis.set_major_formatter(
                            FormatStrFormatter(formatbar) if formatbar is not None
                            else ScalarFormatter())
                        gattr.cax.yaxis.set_major_locator(MaxNLocator(nbins=nticbar))
                    elif formatbar is not None:
                        gattr.cax.yaxis.set_major_formatter(FormatStrFormatter(formatbar))
                    gattr.cax.tick_params(axis='both', which='major',
                                          width=frame_width, colors=frame_color)
                    gattr.cax.set_ylabel(barlabel if barunit is None else f"{barlabel} ({barunit})",
                                         fontsize=barlabelsz, fontweight=barlabelfat, color=frame_color)
                    # if formatbar is not None:
                    #     gattr.cax.yaxis.set_major_formatter(FormatStrFormatter(formatbar))
                    # gattr.cax.yaxis.set_major_locator(MaxNLocator(nbins=n1tic))

                    for iblabel in gattr.cax.yaxis.get_ticklabels():
                        iblabel.set_fontweight(tickfat)
                        iblabel.set_fontsize(ticksz)
            else:
                if movie == 1:gattr.ax2.images[0].set_data(data.window(n1=1, f1=frame1))
                if movie == 2:gattr.ax3.images[0].set_data(data.window(n2=1, f2=frame2))
                if movie == 3:gattr.ax1.images[0].set_data(data.window(n3=1, f3=frame3))
                gattr.set_indicator_frame(gattr, frame1, frame2, frame3)
                fig.canvas.draw_idle()


                if scalebar and datatype == np.uint8 and movie == 3:
                    newbar = bar_arrays.window(n3=1, f3=frame3)
                    new_min_max_vals = np.frombuffer(newbar[:8].tobytes(), dtype=np.float32)
                    new_min, new_max = tuple(new_min_max_vals)
                    gattr.cax.images[0].set_extent([0,1,new_min, new_max])
                    gattr.cax.yaxis.set_major_formatter(
                        FormatStrFormatter(formatbar) if formatbar is not None
                        else ScalarFormatter())
                    gattr.cax.yaxis.set_major_locator(MaxNLocator(nbins=nticbar))

                    gattr.cax.images[0].set_data(255 - newbar[8:, np.newaxis])


                sf_warning(f"Frame {iframe + 1} of {min(nframes, maxframe)};")
                # save or show figure
                if not sys.stdout.isatty():
                    if movie:
                        splitter = (__SVG_SPLITTER[:-3] +
                                    f"framelabel=\"{frame_prefix}{frame_suffix}: " +
                                    f"{frame_axis[iframe * frame_step]:5g} of {frame_axis[-1]:5g}\"" +
                                    __SVG_SPLITTER[-3:])
                        sys.stdout.write(f"\n{splitter}\n")
                        sys.stdout.flush()
                    fig.savefig(sys.stdout.buffer, bbox_inches='tight', format=pformat, dpi=dpi,
                                transparent=None)
                    sys.stdout.flush()
                else:
                    plt.pause(0.01)
                continue


        ax.tick_params(axis='both', which='major', labelsize=ticksz, width=frame_width, colors=frame_color)

        if plottype != 'grey3':
            if format1 is not None:
                try:
                    ax.yaxis.set_major_formatter(FormatStrFormatter(format1))
                except:
                    sf_warning(f"Warning: invalid format1={format1}, ignored.")
            if format2 is not None:
                try:
                    ax.xaxis.set_major_formatter(FormatStrFormatter(format2))
                except:
                    sf_warning(f"Warning: invalid format2={format2}, ignored.")
            if plottype == 'graph':
                if not logx: ax.xaxis.set_major_locator(MaxNLocator(nbins=ntic2))
                if not logy: ax.yaxis.set_major_locator(MaxNLocator(nbins=ntic1))
            else:
                ax.yaxis.set_major_locator(MaxNLocator(nbins=ntic1))
                ax.xaxis.set_major_locator(MaxNLocator(nbins=ntic2))

            label1 = ax.yaxis.get_label()
            label2 = ax.xaxis.get_label()
            ax.set_ylabel(label1.get_text(), fontsize=labelsz, fontweight=labelfat, color=frame_color)
            ax.set_xlabel(label2.get_text(), fontsize=labelsz, fontweight=labelfat, color=frame_color)

            if scalebar:
                if plottype in ['graph', 'wiggle']:
                    # Create empty cmap
                    empty_map = [[0,0,0,0],[0,0,0,0],[0,0,0,0]]
                    norm = mcolors.Normalize(vmin=0, vmax=1)
                    sm = cm.ScalarMappable(norm=norm, cmap=mcolors.ListedColormap(empty_map))
                    sm.set_array([])
                    cbar = fig.colorbar(sm, ax=ax)
                    cbar.ax.patch.set_alpha(0.0)
                    if maxval is not None or minval is not None:
                        cbar.ax.set_ylim(minval, maxval)
                    # Hide it
                    cbar.ax.tick_params(labelsize=ticksz, width=frame_width,colors=[0,0,0,0])  # 或 'white', 'transparent'
                    for clabel in cbar.ax.get_yticklabels():
                        clabel.set_fontweight(tickfat)
                        clabel.set_color([0,0,0,0])
                    for spine in cbar.ax.spines.values():
                        spine.set_edgecolor([0,0,0,0])
                        spine.set_linewidth(frame_width)
                    if barlabel:
                        cbar.set_label(barlabel if barunit is None else f"{barlabel} ({barunit})",
                                       fontsize=barlabelsz, fontweight=barlabelfat, color=[0,0,0,0])
                else:
                    cbar = fig.colorbar(ax.images[0], ax=ax)
                    if datatype == np.uint8:
                        cbar.ax.clear()
                        cbar.ax.imshow(255 - bar_array[8:, np.newaxis], aspect='auto', cmap=color,
                                       vmin=0, vmax=255, extent=[0,1,min_val, max_val])
                        cbar.ax.xaxis.set_visible(False)
                        cbar.ax.yaxis.set_label_position('right')
                        cbar.ax.yaxis.set_major_formatter(FuncFormatter(formatbar) if formatbar is not None
                                                          else ScalarFormatter())
                    elif formatbar is not None:
                        cbar.ax.yaxis.set_major_formatter(FormatStrFormatter(formatbar))
                        if maxval is not None or minval is not None:
                            vmin, vmax = ax.images[0].get_clim()
                            if minval is None: minval = vmin
                            if maxval is None: maxval = vmax
                            cbar.ax.set_ylim(minval, maxval)
                    cbar.ax.tick_params(labelsize=ticksz, width=frame_width, colors=frame_color)
                    for ticklabel in cbar.ax.get_yticklabels():
                        ticklabel.set_fontweight(tickfat)
                    if barlabel:
                        cbar.set_label(barlabel if barunit is None else f"{barlabel} ({barunit})",
                                       fontsize=barlabelsz, fontweight=barlabelfat, color=frame_color)
                    for spine in cbar.ax.spines.values():
                        spine.set_edgecolor(frame_color)
                        spine.set_linewidth(frame_width)


                    

                offset = cbar.ax.yaxis.get_offset_text()
                offset.set_color(frame_color)
                offset.set_fontsize(ticksz)
                offset.set_fontweight(tickfat)


            if title:
                if titleloc:
                    try:ax.set_title(title,
                                fontsize=titlesz,
                                fontweight=titlefat,
                                color=frame_color,
                                y=titleloc)
                    except:
                        sf_warning(f"Warning: invalid titleloc={titleloc}, need float.")
                        ax.set_title(title,
                                fontsize=titlesz,
                                fontweight=titlefat,
                                color=frame_color)
                else:ax.set_title(title,
                                    fontsize=titlesz,
                                    fontweight=titlefat,
                                    color=frame_color)
            if label1loc in ['left', 'right']:
                ax.yaxis.set_label_position(label1loc)
            else:
                sf_warning(f"Warning: invalid whereylabel={label1loc}, use default left.")
            if label2loc in ['top', 'bottom']:
                ax.xaxis.set_label_position(label2loc)
            else:
                sf_warning(f"Warning: invalid wherexlabel={label2loc}, use default bottom.")
            if tick1loc in ['left', 'right']:
                ax.yaxis.set_ticks_position(tick1loc)
            else:
                sf_warning(f"Warning: invalid whereytick={tick1loc}, use default left.")
            if tick2loc in ['top', 'bottom']:
                ax.xaxis.set_ticks_position(tick2loc)
            else:
                sf_warning(f"Warning: invalid wherextick={tick2loc}, use default bottom.")


            for ticklabel in (ax.get_xticklabels()+ax.get_yticklabels()):
                ticklabel.set_fontweight(tickfat)

            if gridon:
                ax.grid(visible=True, color=frame_color, linestyle=gridstyle, linewidth=frame_width, alpha=0.5)

            for spine in ax.spines.values():
                spine.set_edgecolor(frame_color)
                spine.set_linewidth(frame_width)


        # Addon elements
        ## Rectangles (at most 10)
        for irect in range(10):
            rectpar = par_dict.get(f'rect{irect+1}', None)
            if rectpar is None: continue
            try:
                rect_vals = rectpar.split(',')
                if not (4 <= len(rect_vals)):
                    sf_warning(f"Warning: invalid rect{irect+1}={rectpar}, ignored.")
                    continue
                min1, max1, min2, max2 = [float(v) for v in rect_vals[:4]]
                rectcolor = par_dict.get(f'rect{irect+1}color', par_dict.get('rectcolor', frame_color))
                rectwidth = getfloat(par_dict, f'rect{irect+1}width', getfloat(par_dict, 'rectwidth', frame_width))
                rectstyle = par_dict.get(f'rect{irect+1}style', par_dict.get('rectstyle', '-'))
                rect_alpha = getfloat(par_dict, f'rect{irect+1}alpha', getfloat(par_dict, 'rectalpha', 1.0))
                xy = (min2, min1)
                width = max2 - min2
                height = max1 - min1
                rect = plt.Rectangle(xy, width, height, fill=False, edgecolor=rectcolor, linewidth=rectwidth, linestyle=rectstyle, alpha=rect_alpha)
                ax.add_patch(rect)
            except:
                sf_warning(f"Warning: invalid rect{irect+1}={rectpar}, ignored.")
                continue
        ## Arrows (at most 10)
        for iarrow in range(10):
            arrowpar = par_dict.get(f'arrow{iarrow+1}', None)
            if arrowpar is None:
                continue
            try:
                arrow_vals = arrowpar.split(',')
                if not (4 <= len(arrow_vals)):
                    sf_warning(f"Warning: invalid arrow{iarrow+1}={arrowpar}, ignored.")
                    continue

                y0, x0, dy, dx = [float(v) for v in arrow_vals[:4]]

                arrow_color = par_dict.get(f'arrow{iarrow+1}color', par_dict.get('arrowcolor', frame_color))
                arrow_width = getfloat(par_dict, f'arrow{iarrow+1}width', getfloat(par_dict, 'arrowwidth', 0.1))
                arrow_alpha = par_dict.get(f'arrow{iarrow+1}alpha', par_dict.get('arrowalpha', 1.0))

                arr = plt.Arrow(x0, y0, dx, dy,
                                width=arrow_width,
                                color=arrow_color,
                                alpha=arrow_alpha)

                ax.add_patch(arr)

            except Exception as e:
                sf_warning(f"Warning: invalid arrow{iarrow+1}={arrowpar} {e}, ignored.")
                continue
        ## Texts (at most 10)
        for itxt in range(10):
            txtpar = par_dict.get(f'text{itxt+1}', None)
            if txtpar is None:
                continue
            try:
                txt_vals = txtpar.split(',')
                if not (3 <= len(txt_vals)):
                    sf_warning(f"Warning: invalid text{itxt+1}={txtpar}, ignored.")
                    continue

                x0 = float(txt_vals[1])
                y0 = float(txt_vals[0])
                text_str = str(txt_vals[2])

                text_color = par_dict.get(f'text{itxt+1}color', par_dict.get('textcolor', frame_color))
                text_size  = getfloat(par_dict, f'text{itxt+1}size', getfloat(par_dict, 'textsize', fontsz))
                text_weight = par_dict.get(f'text{itxt+1}weight', par_dict.get('textweight', fontweight))
                text_facecolor = par_dict.get(f'text{itxt+1}facecolor', par_dict.get('textfacecolor', 'none'))
                text_edgecolor = par_dict.get(f'text{itxt+1}edgecolor', par_dict.get('textedgecolor', 'none'))
                text_alpha = getfloat(par_dict, f'text{itxt+1}alpha', getfloat(par_dict, 'textalpha', 1.0))

                ax.text(x0, y0, text_str,
                        color=text_color,
                        fontsize=text_size,
                        fontweight=text_weight,
                        bbox=dict(facecolor=text_facecolor,alpha=text_alpha, edgecolor=text_edgecolor),
                        alpha=text_alpha)

            except Exception as e:
                sf_warning(f"Warning: invalid text{itxt+1}={txtpar}: {e}, ignored.")
                continue



        plt.tight_layout()

        if movie: sf_warning(f"Frame {iframe + 1} of {min(nframes, maxframe)};")
        # save or show figure
        if not sys.stdout.isatty():
            if movie:
                splitter = (__SVG_SPLITTER[:-3] +
                            f"framelabel=\"{frame_prefix}{frame_suffix}: " +
                            f"{frame_axis[iframe * frame_step]:5g} of {frame_axis[-1]:5g}\"" +
                            __SVG_SPLITTER[-3:])
                sys.stdout.write(f"\n{splitter}\n")
                sys.stdout.flush()
            fig.savefig(sys.stdout.buffer, bbox_inches='tight', format=pformat, dpi=dpi,
                        transparent=None)
            sys.stdout.flush()
        else:
            if movie: plt.pause(0.01)
            else: plt.show()
    if not sys.stdout.isatty():
        sys.stdout.buffer.close()
    else:
        plt.show()

    plt.close(fig)
    sys.exit(0)
    

    
## Safely get float parameters
def getfloat(par_dict, parname, default):
    try:
        val = par_dict.get(parname, default)
        if val is not None: val = float(val)
    except ValueError:
        sf_warning(f"Warning: invalid {parname}={par_dict.get(parname)}, use default {default}.")
        val = default
    return val

def sf_warning(*args, **kwargs):
    endl = ''
    file = kwargs.pop('file', sys.stderr)
    try:
        if args[-1].endswith('.'): endl = '\n'
        if args[-1].endswith(';'): endl = '\r'
    except:
        if isinstance(args, str):
            if args.endswith('.'): endl = '\n'
            if args.endswith(';'): endl = '\r'
    endl = kwargs.pop('end', endl)
    sep = kwargs.pop('sep', '')
    print(f'{__progname__}:', *args, sep=sep, file=file, end=endl, **kwargs)


def sf_error(*args, **kwargs):
    sf_warning(*args, **kwargs)
    sys.exit(1)


if __name__ == "__main__":
    main()