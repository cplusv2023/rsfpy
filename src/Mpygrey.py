#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
\033[1mNAME\033[0m
    \tMpygrey.py
\033[1mDESCRIPTION\033[0m
    \tdisplay RSF data as a grey or color image using matplotlib.
\033[1mSYNOPSIS\033[0m
    \tMpygrey.py < in.rsf [> out.[pdf|png|jpg|...]] transp=y yreverse=y xreverse=n clip= pclip= allpos=n bias=0. verb=n scalebar=n color=gray bgcolor=w screenwidth=8. screenheight=6. dpi=100. label1= label2= title=
\033[1mCOMMENTS\033[0m
    \tInput data is from stdin, output figure is to stdout.

    \tIf output is not redirected to a file, the figure will be shown in a window.

    \tcolor is a matplotlib colormap name, e.g., gray, jet, seismic, hot, cool, etc.

    \tfontfat is font weight: light, normal, medium, semibold, demibold, bold, heavy, ultralight, black, regular, book,  black
\033[1mPARAMETERS\033[0m
    \t\033[4mbool\033[0m\t\033[1mallpos=n\033[0m [y/n] if y, assume positive data
    \t\033[4mstring\033[0m\t\033[1mbarlabel=\033[0m colorbar label
    \t\033[4mstring\033[0m\t\033[1mbarlabelfat=normal\033[0m colorbar label font weight: normal, bold, light, etc.
    \t\033[4mfloat\033[0m\t\033[1mbarlabelsz=12.\033[0m colorbar label font size
    \t\033[4mfloat\033[0m\t\033[1mbgcolor=w\033[0m background color (w: white, k: black)
    \t\033[4mstring\033[0m\t\033[1mbias=0.\033[0m value mapped to the center of the color table
    \t\033[4mfloat\033[0m\t\033[1mclip=\033[0m data clip
    \t\033[4mstring\033[0m\t\033[1mcolor=gray\033[0m color scheme (matplotlib colormap name, e.g., gray, jet, seismic, hot, cool, etc)
    \t\033[4mfloat\033[0m\t\033[1mdpi=100.\033[0m figure resolution in dots per inch
    \t\033[4mstring\033[0m\t\033[1mfont=sans-serif\033[0m font family
    \t\033[4mstring\033[0m\t\033[1mformat=pdf\033[0m output figure format: pdf, png, jpg, etc
    \t\033[4mstring\033[0m\t\033[1mformat1=\033[0m format for axis 1 tick labels, e.g., %.2f, %.3e, etc.
    \t\033[4mstring\033[0m\t\033[1mformat2=\033[0m format for axis 2 tick labels, e.g., %.2f, %.3e, etc.
    \t\033[4mstring\033[0m\t\033[1mformatbar=\033[0m format for colorbar tick labels, e.g., %.2f, %.3e, etc.
    \t\033[4mstring\033[0m\t\033[1mframecolor=k\033[0m frame color
    \t\033[4mfloat\033[0m\t\033[1mframewidth=1.0\033[0m frame line width
    \t\033[4mbool\033[0m\t\033[1mgrid=n\033[0m [y/n] if y, show grid
    \t\033[4mstring\033[0m\t\033[1mgridstyle=--\033[0m grid line style
    \t\033[4mstring\033[0m\t\033[1mlabelfat=normal\033[0m label font weight: normal, bold, light, etc.
    \t\033[4mfloat\033[0m\t\033[1mlabelsz=12.\033[0m label font size
    \t\033[4mfloat\033[0m\t\033[1mmin1=\033[0m minimum value of axis 1 (overrides header d1 and o1)
    \t\033[4mfloat\033[0m\t\033[1mmin2=\033[0m minimum value of axis 2 (overrides header d2 and o2)
    \t\033[4mstring\033[0m\t\033[1mmax1=\033[0m maximum value of axis 1 (overrides header d1 and o1)
    \t\033[4mstring\033[0m\t\033[1mmax2=\033[0m maximum value of axis 2 (overrides header d2 and o2)
    \t\033[4mfloat\033[0m\t\033[1mntic1=5\033[0m max number of ticks on axis 1
    \t\033[4mfloat\033[0m\t\033[1mntic2=5\033[0m max number of ticks on axis 2
    \t\033[4mfloat\033[0m\t\033[1mpclip=99.\033[0m data clip percentile (default is 99)
    \t\033[4mbool\033[0m\t\033[1mscalebar=n\033[0m [y/n] if y, draw scalebar
    \t\033[4mfloat\033[0m\t\033[1mscreenheight=6.\033[0m figure height in inches
    \t\033[4mfloat\033[0m\t\033[1mscreenwidth=8.\033[0m figure width in inches
    \t\033[4mstring\033[0m\t\033[1mtickfat=normal\033[0m tick font weight: normal, bold, light, etc.
    \t\033[4mfloat\033[0m\t\033[1mticksz=10.\033[0m tick font size
    \t\033[4mstring\033[0m\t\033[1mtitlefat=bold\033[0m title font weight: normal, bold, light, etc.
    \t\033[4mfloat\033[0m\t\033[1mtitlesz=14.\033[0m title font size
    \t\033[4mbool\033[0m\t\033[1mtransp=y\033[0m [y/n] if y, transpose the display axes
    \t\033[4mbool\033[0m\t\033[1mverb=n\033[0m [y/n] verbosity flag
    \t\033[4mbool\033[0m\t\033[1mwheretitle=top\033[0m title position: top, bottom
    \t\033[4mstring\033[0m\t\033[1mwhereylabel=left\033[0m axis 1 label position: left, right
    \t\033[4mstring\033[0m\t\033[1mwherexlabel=bottom\033[0m axis 2 label position: top, bottom
    \t\033[4mstring\033[0m\t\033[1mwherextick=bottom\033[0m horizontal axis tick position: top, bottom
    \t\033[4mstring\033[0m\t\033[1mwhereytick=left\033[0m vertical axis tick position: left, right
    \t\033[4mbool\033[0m\t\033[1mxreverse=n\033[0m [y/n] if y, reverse the horizontal axis
    \t\033[4mbool\033[0m\t\033[1myreverse=y\033[0m [y/n] if y, reverse the vertical axis

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
    \t\033[4mstring\033[0m\t\033[1mtext#weight=normal\033[0m text font weight: normal, bold, light, etc. (default is fontfat)
    \t\033[4mstring\033[0m\t\033[1mtext#facecolor=none\033[0m text box face color (default is none)
    \t\033[4mstring\033[0m\t\033[1mtext#edgecolor=none\033[0m text box edge color (default is none)
    \t\033[4mfloat\033[0m\t\033[1mtext#alpha=0.5\033[0m text box alpha (default is 0.5)


    
"""



import numpy as np
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import matplotlib.colors as mcolors
import sys, subprocess, os, re
from textwrap import dedent

from webencodings import labels

from rsfpy import Rsfarray
from rsfpy.utils import _str_match_re
from matplotlib.ticker import MaxNLocator, FormatStrFormatter, LogLocator

__progname__ = os.path.basename(sys.argv[0])
DOC = dedent(__doc__.replace('Mpygrey.py', __progname__))
VERB = True

def main():
    if len(sys.argv) < 2 and sys.stdin.isatty():
        subprocess.run(['less', '-R'], input=DOC.encode())
        sys.exit(1)
    par_dict = _str_match_re(sys.argv[1:])

    # Check stdin
    if sys.stdin.isatty():
        sf_error("Error: no input data?")

    # Read data
    data = Rsfarray(sys.stdin.buffer)
    if data.size == 0:
        sf_error("Failed read RSF data from input.")
    
    # Get parameters
    transp = par_dict.get('transp', 'y').lower().startswith('y')
    yreverse = par_dict.get('yreverse', 'y').lower().startswith('y')
    xreverse = par_dict.get('xreverse', 'n').lower().startswith('y')
    allpos = par_dict.get('allpos', 'n').lower().startswith('y')
    VERB = par_dict.get('verb', 'n').lower().startswith('y')
    scalebar = par_dict.get('scalebar', 'n').lower().startswith('y')
    color = par_dict.get('color', 'gray')
    label1 = par_dict.get('label1', None)
    label2 = par_dict.get('label2', None)
    title = par_dict.get('title', data.header.get('title', ''))
    clip = getfloat(par_dict, 'clip', None)
    pclip = getfloat(par_dict, 'pclip', 99.)
    bias = getfloat(par_dict, 'bias', 0.)
    fig_width = getfloat(par_dict, 'screenwidth', 8.)
    fig_height = getfloat(par_dict, 'screenheight', 6.)
    facecolor = par_dict.get('bgcolor', 'w')
    dpi = getfloat(par_dict, 'dpi', 100.)
    fontsz = getfloat(par_dict, 'fontsz', 12.)
    font_family = par_dict.get('font', 'sans-serif')
    fontweight = par_dict.get('fontfat', 'normal')
    labelsz = getfloat(par_dict, 'labelsz', fontsz)
    titlesz = getfloat(par_dict, 'titlesz', fontsz)
    ticksz = getfloat(par_dict, 'ticksz', fontsz)
    labelfat = par_dict.get('labelfat', fontweight)
    titlefat = par_dict.get('titlefat', fontweight)
    tickfat = par_dict.get('tickfat', fontweight)
    gridon = par_dict.get('grid', 'n').lower().startswith('y')
    gridstyle = par_dict.get('gridstyle', '--')
    frame_color = par_dict.get('framecolor', 'k')
    frame_width = getfloat(par_dict, 'framewidth', 1.0)
    ntic1 = getfloat(par_dict, 'ntic1', 5)
    ntic2 = getfloat(par_dict, 'ntic2', 5)
    min1 = getfloat(par_dict, 'min1', None)
    min2 = getfloat(par_dict, 'min2', None)
    max1 = getfloat(par_dict, 'max1', None)
    max2 = getfloat(par_dict, 'max2', None)
    maxval = getfloat(par_dict, 'maxval', None)
    minval = getfloat(par_dict, 'minval', None)
    label1loc = par_dict.get('whereylabel', 'left').lower()
    label2loc = par_dict.get('wherexlabel', 'bottom').lower()
    tick1loc = par_dict.get('whereytick', label1loc).lower()
    tick2loc = par_dict.get('wherextick', label2loc).lower()
    titleloc = getfloat(par_dict, 'wheretitle', None)
    format1 = par_dict.get('format1', None)
    format2 = par_dict.get('format2', None)
    formatbar = par_dict.get('formatbar', None)
    barlabel = par_dict.get('barlabel', '')
    barlabelfat = par_dict.get('barlabelfat', fontweight)
    barlabelsz = getfloat(par_dict, 'barlabelsz', fontsz)
    pformat = par_dict.get('format', 'svg')
    wantlabel1 = par_dict.get('wantlabel1', 'y').lower().startswith('y')
    wantlabel2 = par_dict.get('wantlabel2', 'y').lower().startswith('y')

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
        # Parameters for wiggle plot
        zplot = getfloat(par_dict, 'zplot', 1.0)
        ncolor = par_dict.get('ncolor', 'none')
        lcolor = par_dict.get('lcolor', 'k')
        pcolor = par_dict.get('pcolor', lcolor)
        if par_dict.get('fill', 'y').lower().startswith('n'):
            ncolor = 'none'
            pcolor = 'none'
        plotfat = getfloat(par_dict, 'linewidth', 
                           getfloat(par_dict, 'plotfat', frame_width))
    elif plottype == 'graph':
        # Parameters for graph plot
        transp = par_dict.get('transp', 'n').lower().startswith('y')
        yreverse = par_dict.get('yreverse', 'n').lower().startswith('y')
        lcolor = par_dict.get('lcolor', 'k')
        lcolors = par_dict.get('lcolors', None)
        lstyle = par_dict.get('lstyle', None)
        lstyles = par_dict.get('lstyles', None)
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
        # scalebar = False
    elif plottype == 'grey3':
        frame1 = int(getfloat(par_dict, 'frame1', 0.0))
        frame2 = int(getfloat(par_dict, 'frame2', 0.0))
        frame3 = int(getfloat(par_dict, 'frame3', 0.0))
        if frame1 > data.n1: frame1 = data.n1
        elif frame1 < 0: frame1 = 0
        if frame2 > data.n2: frame2 = data.n2
        elif frame2 < 0: frame2 = 0
        if frame3 > data.n3: frame3 = data.n3
        elif frame3 < 0: frame3 = 0
        point1 = getfloat(par_dict, 'point1', 0.8)
        point2 = getfloat(par_dict, 'point2', 0.4)
        isflat = par_dict.get('flat', 'y').lower().startswith('y')
        label3 = par_dict.get('label3', None)
        wantlabel3 = par_dict.get('wantlabel3', 'y').lower().startswith('y')


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

    if not wantlabel1: label1 = " "
    if not wantlabel2: label2 = " "

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
 


    plt.rcParams['font.family'] = font_family

    fig = plt.figure(figsize=(fig_width, fig_height), dpi=dpi, facecolor=facecolor)
    ax = fig.add_subplot(1, 1, 1)

    if plottype == 'grey':
        
        data.grey(ax=ax, transp=transp, yreverse=yreverse, xreverse=xreverse,
                allpos=allpos, clip=clip, pclip=pclip, bias=bias, cmap=color, 
                min1=min1, max1=max1, min2=min2, max2=max2,
                colorbar=False, label1=label1, label2=label2, show=False)
    elif plottype == 'wiggle':
        data.wiggle(ax=ax, 
                transp=transp, yreverse=yreverse, xreverse=xreverse,
                min1=min1, max1=max1, min2=min2, max2=max2,
                label1=label1, label2=label2, 
                zplot=zplot, bias=bias, clip=clip, pclip=pclip,
                ncolor=ncolor, pcolor=pcolor, lcolor=lcolor,
                linewidth=plotfat,
                show=False)
    elif plottype == 'graph':
        if data.ndim < 2:
            data = data.reshape((data.n1, 1))
        for itrace in range(data.n2):
            if transp:
                x = data[:, itrace].squeeze()
                y = data.axis1
            else:
                x = data.axis1
                y = data[:, itrace].squeeze()
            
            ax.plot(x, y, color=lcolors[itrace], linewidth=plotfat,
                    linestyle=lstyles[itrace],                    
                    label=legends[itrace] if legends else None)

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
            ax.set_xlabel(label1 if label1 else data.label_unit(0))
            ax.set_ylabel(label2 if label2 else data.label_unit(1))
        else:
            ax.set_ylabel(label1 if label1 else data.label_unit(0))
            ax.set_xlabel(label2 if label2 else data.label_unit(1))


    elif plottype == 'grey3':
        if not wantlabel3: label3 = " "

        gattr = data.grey3(ax=ax, frame1=frame1, frame2=frame2, frame3=frame3,
                   point1=point1, point2=point2, colorbar=scalebar, cmap=color,
                   label1=label1, label2=label2, label3=label3,
                   clip=clip, pclip=pclip,bias=bias, allpos=allpos,
                           title=title,
                   flat=isflat, show=False)
        gattr.set_title(title, fontsize=titlesz, fontweight=titlefat)
        gattr.set_lines(color=frame_color,width=frame_width)
        gattr.set_spines(color=frame_color,width=frame_width)
        gattr.set_ticklabels(color=frame_color,fontsize=ticksz, weight=tickfat)
        gattr.set_labels(color=frame_color,fontsize=labelsz, weight=labelfat)
        gattr.cax.tick_params(axis='both', which='major',
                              width=frame_width, colors=frame_color)
        for iblabel in gattr.cax.yaxis.get_ticklabels():
            iblabel.set_fontweight(tickfat)
            iblabel.set_fontsize(ticksz)

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
                    cbar.set_label(barlabel, fontsize=barlabelsz, fontweight=barlabelfat, color=[0,0,0,0])
            else:
                cbar = fig.colorbar(ax.images[0], ax=ax)
                if maxval is not None or minval is not None:
                    vmin, vmax = ax.images[0].get_clim()
                    if minval is None: minval = vmin
                    if maxval is None: maxval = vmax
                    cbar.ax.set_ylim(minval, maxval)
                cbar.ax.tick_params(labelsize=ticksz, width=frame_width, colors=frame_color)
                for ticklabel in cbar.ax.get_yticklabels():
                    ticklabel.set_fontweight(tickfat)
                if barlabel:
                    cbar.set_label(barlabel, fontsize=barlabelsz, fontweight=barlabelfat, color=frame_color)
                for spine in cbar.ax.spines.values():
                    spine.set_edgecolor(frame_color)
                    spine.set_linewidth(frame_width)

            if formatbar is not None:
                try:
                    cbar.ax.yaxis.set_major_formatter(FormatStrFormatter(formatbar))
                except:
                    sf_warning(f"Warning: invalid formatbar={formatbar}, ignored.")

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

    # save or show figure
    if sys.stdout.isatty():
        plt.show()
    else:
        outfile = sys.stdout.buffer
        fig.savefig(outfile, bbox_inches='tight', format=pformat, dpi=dpi, transparent=(not facecolor or facecolor=='none'))
        outfile.flush()
        outfile.close()
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
    verb = kwargs.pop('verb', VERB)
    endl = ''
    file = kwargs.pop('file', sys.stderr)
    if isinstance(args, str) and args[-1] is not None:
        if args[-1].endswith('.'): endl = '\n' 
        if args[-1].endswith(';'): endl = '\r'
    endl = kwargs.pop('end', endl)
    if verb: print(f'{__progname__}:', *args, file=file, end=endl, **kwargs)


def sf_error(*args, **kwargs):
    sf_warning(*args, verb=True, **kwargs)
    sys.exit(1)


if __name__ == "__main__":
    main()