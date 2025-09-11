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
__doc__ = """
\033[1mNAME\033[0m
"""


from lxml import etree
import math, sys, os, subprocess
from textwrap import dedent
from rsfpy.utils import _str_match_re


DOC = dedent(__doc__)
VERB = True

def main():
    if len(sys.argv) < 2 and sys.stdin.isatty():
        subprocess.run(['less', '-R'], input=DOC.encode())
        sys.exit(1)
    par_dict = _str_match_re(' '.join(sys.argv[1:]))
    # Check stdin
    if sys.stdin.isatty():
        inputs = []
    else:
        inputs = [sys.stdin]
    
    for isvg in sys.argv[1:]:
        if os.path.isfile(isvg) and isvg.lower().endswith('.svg'):
            inputs.append(isvg)
    if len(inputs) < 1:
        sf_error("Error: no input SVG files.")
    
    ncol = int(getfloat(par_dict, 'ncol', 3))
    nrow = int(getfloat(par_dict, 'nrow', -1))
    stretchx = par_dict.get('stretchx', 'false').lower() in ('true', 'y', 'yes')
    stretchy = par_dict.get('stretchy', 'true').lower() in ('true', 'y', 'yes')
    mode = par_dict.get('mode', 'grid').lower()
    bgcolor = par_dict.get('bgcolor', None)
    label = par_dict.get('label', 'y').lower() in ('true', 'y', 'yes')
    labelfont = par_dict.get('labelfont', None)
    labelcolor = par_dict.get('labelcolor', 'k')
    labelfat = par_dict.get('labelfat', 'normal')
    loc = par_dict.get('labelloc', 'north west').lower()
    labeltype = par_dict.get('labeltype', 'a')
    labelformat = par_dict.get('labelformat', '(%s)')
    labelsz = getfloat(par_dict, 'labelsz', getfloat(par_dict, 'labelsize', 12))
    labelmargin = int(getfloat(par_dict, 'labelmargin', 10))

    
    colortable={
        'black': '#000000',
        'white': '#FFFFFF',
        'red': '#FF0000',
        'green': '#00FF00',
        'blue': '#0000FF',
        'yellow': '#FFFF00',
        'cyan': '#00FFFF',
        'magenta': '#FF00FF',
        'gray': '#808080',
        'grey': '#808080',
        'k': '#000000', 
        'w': '#FFFFFF',
        'r': '#FF0000',
        'g': '#00FF00',
        'b': '#0000FF',
        'y': '#FFFF00',
        'c': '#00FFFF',
        'm': '#FF00FF',
    }
    if bgcolor in colortable: bgcolor = colortable[bgcolor]
    if label:
        if labelcolor in colortable: labelcolor = colortable[labelcolor]
        if loc not in ('north west', 'north east', 'south west', 'south east', 'north', 'south', 'west', 'east'):
            sf_warning(f"Warning: invalid loc={loc}, use default 'north west'.")
            loc = 'north west'
        if labeltype not in ('a', '#', 'I'):
            sf_warning(f"Warning: invalid labeltype={labeltype}, use default 'a'.")
            labeltype = 'a'
        if '%s' not in labelformat:
            sf_warning(f"Warning: invalid labelformat={labelformat}, use default '(%s)'.")
            labelformat = '(%s)'


    if mode == 'grid':
        outtree = grid(inputs, ncol=ncol, nrow=nrow, stretchx=stretchx, stretchy=stretchy, bgcolor=bgcolor, label=label, loc=loc, labeltype=labeltype, labelformat=labelformat, labelmargin=labelmargin,
                       labelfont=labelfont, labelcolor=labelcolor, labelsz=labelsz, labelfat=labelfat)
    elif mode == 'overlay':
        outtree = overlay(inputs, bgcolor=bgcolor)
    else:
        sf_error(f"Error: unsupported mode={mode}.")
    outtree.write(sys.stdout.buffer, pretty_print=True, xml_declaration=True, encoding='utf-8')
    

def grid(inputs, ncol=3, nrow=-1, stretchx=False, stretchy=True, bgcolor=None,label=False, loc="north west",    labeltype="a", labelformat="(%s)", labelmargin=10,
         labelfont=None, labelcolor="#000", labelsz=12, labelfat="normal"):
    if ncol == -1 and nrow == -1:
        ncol, nrow = 3, -1

    total = len(inputs)
    if ncol != -1 and nrow != -1 and ncol * nrow < total:
        ncol, nrow = 3, -1

    if ncol == -1:
        ncol = math.ceil(total / nrow) if nrow != -1 else 3
    if nrow == -1:
        nrow = math.ceil(total / ncol)

    current_ncol = math.ceil(total / nrow)

    svgs = [etree.parse(f).getroot() for f in inputs]

    sizes = []
    for svg in svgs:
        w = float(svg.attrib.get("width", "100").replace("pt", "").replace("px", ""))
        h = float(svg.attrib.get("height", "100").replace("pt", "").replace("px", ""))
        sizes.append((w, h))

    col_widths = [0] * current_ncol
    row_heights = [0] * nrow
    for idx, (w, h) in enumerate(sizes):
        row = idx // current_ncol
        col = idx % current_ncol
        col_widths[col] = max(col_widths[col], w)
        row_heights[row] = max(row_heights[row], h)

    if stretchx:
        max_width = max(col_widths)
        col_widths = [max_width] * current_ncol
    if stretchy:
        max_height = max(row_heights)
        row_heights = [max_height] * nrow

    SVG_NS = "http://www.w3.org/2000/svg"
    root = etree.Element("{%s}svg" % SVG_NS, nsmap={None: SVG_NS})
    total_width = sum(col_widths)
    total_height = sum(row_heights)
    root.attrib["width"] = f'{total_width}pt'
    root.attrib["height"] = f'{total_height}pt'

    for idx, svg in enumerate(svgs):
        row = idx // ncol
        col = idx % ncol
        x_offset = sum(col_widths[:col]) * 4./3.
        y_offset = sum(row_heights[:row]) * 4./3.
        orig_w, orig_h = sizes[idx]
        target_w = col_widths[col]
        target_h = row_heights[row]

        scale_x = target_w / orig_w if stretchx else 1
        scale_y = target_h / orig_h if stretchy else 1
        # g = etree.Element("g")
        svg.attrib["transform"] = f"translate({x_offset},{y_offset}) scale({scale_x},{scale_y}) "
        svg.attrib["vector-effect"] = f"non-scaling-stroke"
        # g.attrib["width"] = f'{target_w}pt'
        # g.attrib["height"] = f'{target_h}pt'

        for child in svg:
            clean_fill_recursive(child)
            # g.append(child)


        root.append(svg)
        
        if label:
            if labeltype == "a":
                label_text = chr(ord("a") + idx)
            elif labeltype == "#":
                label_text = str(idx + 1)
            elif labeltype == "I":
                roman = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
                        "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX"]
                label_text = roman[idx] if idx < len(roman) else str(idx + 1)
            else:
                label_text = str(idx + 1)

            label_text = labelformat % label_text

            offset_x = x_offset
            offset_y = y_offset
            margin = labelmargin * (4./3.)

            if loc == "north west":
                offset_x += margin
                offset_y += margin
            elif loc == "north east":
                offset_x += target_w - margin
                offset_y += margin
            elif loc == "south west":
                offset_x += margin
                offset_y += target_h - margin
            elif loc == "south east":
                offset_x += target_w - margin
                offset_y += target_h - margin
            elif loc == "north":
                offset_x += target_w / 2
                offset_y += margin
            elif loc == "south":
                offset_x += target_w / 2
                offset_y += target_h - margin
            elif loc == "west":
                offset_x += margin
                offset_y += target_h / 2
            elif loc == "east":
                offset_x += target_w - margin
                offset_y += target_h / 2

            text = etree.Element("text", {
                "x": str(offset_x),
                "y": str(offset_y),
                "fill": labelcolor,
                "font-weight": labelfat,
                "font-size": f'{labelsz}pt',
                "text-anchor": "start" if "west" in loc else ("end" if "east" in loc else "middle"),
                "dominant-baseline": "hanging" if "north" in loc else ("auto" if "south" in loc else "central"),
            })
            if labelfont:
                text.attrib["font-family"] = labelfont

            text.text = label_text
            root.append(text)


    if bgcolor is not None:
        bg_rect = etree.Element("rect", {
            "x": "0",
            "y": "0",
            "width": f'{total_width}pt',
            "height": f'{total_height}pt',
            "fill": bgcolor
        })
        root.insert(0, bg_rect)

    return etree.ElementTree(root)

def overlay(inputs, bgcolor=None):
    svgs = [etree.parse(f).getroot() for f in inputs]
    SVG_NS = "http://www.w3.org/2000/svg"
    root = etree.Element("{%s}svg" % SVG_NS, nsmap={None: SVG_NS})
    max_w = 0
    max_h = 0
    for svg in svgs:
        w = float(svg.attrib.get("width", "100").replace("pt", "").replace("px", ""))
        h = float(svg.attrib.get("height", "100").replace("pt", "").replace("px", ""))
        max_w = max(max_w, w)
        max_h = max(max_h, h)
    root.attrib["width"] = f'{max_w}pt'
    root.attrib["height"] = f'{max_h}pt'
    for svg in svgs:
        clean_fill_recursive(svg)
        root.append(svg)
    if bgcolor is not None:
        bg_rect = etree.Element("rect", {
            "x": "0",
            "y": "0",
            "width": f'{max_w}pt',
            "height": f'{max_h}pt',
            "fill": bgcolor
        })
        root.insert(0, bg_rect)
    return etree.ElementTree(root)


def clean_fill_recursive(elem):
    elem_id = elem.attrib.get("id", "")
    if elem_id.startswith("patch_1") or elem_id.startswith("patch_2"):
        for child in elem:
            if 'fill' in child.attrib.get('style', ''):
                styles = child.attrib['style'].split(';')
                styles = [s for s in styles if not s.strip().startswith('fill:')]
                styles.append('fill: none')
                child.attrib['style'] = ';'.join(styles)

    for child in elem:
        clean_fill_recursive(child)

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
    if args[-1] is not None:
        if args[-1].endswith('.'): endl = '\n' 
        if args[-1].endswith(';'): endl = '\r'
    endl = kwargs.pop('end', endl)
    if verb: print(f'{__file__}:', *args, file=file, end=endl, **kwargs)


def sf_error(*args, **kwargs):
    sf_warning(*args, verb=True, **kwargs)
    sys.exit(1)


if __name__ == "__main__":
    main()