#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__doc__ = """
\033[1mNAME\033[0m
    \tMsvgpen.py
\033[1mDESCRIPTION\033[0m
    \tArrange multiple SVG files into a single SVG file in a grid or overlay mode.
\033[1mSYNOPSIS\033[0m
    \tMsvgpen.py [< in0.svg] in1.svg in2.svg ... [> out.svg] ncol=3 nrow=-1 stretchx=n stretchy=y mode=grid bgcolor= label=y labelfont= labelcolor=k labelfat=normal loc="north west" labeltype=a labelformat="(%s)" labelsz=12. labelmargin=10
\033[1mCOMMENTS\033[0m
    \tlabel refers to subplot labels, not axis labels. Only works in grid mode.
    \tInput SVG files can be specified as command line arguments or from stdin.
\033[1mPARAMETERS\033[0m
    \t\033[4mint\033[0m\t\033[1mncol=3\033[0m number of columns (default is 3)
    \t\033[4mint\033[0m\t\033[1mnrow=-1\033[0m number of rows (default is -1, which means auto)
    \t\033[4mbool\033[0m\t\033[1mstretchx=n\033[0m [y/n] if y, stretch subplots to have the same width (only in grid mode)
    \t\033[4mbool\033[0m\t\033[1mstretchy=y\033[0m [y/n] if y, stretch subplots to have the same height (only in grid mode)
    \t\033[4mstring\033[0m\t\033[1mmode=grid\033[0m arrangement mode: grid, overlay or movie
    \t\033[4mstring\033[0m\t\033[1mbgcolor=\033[0m background color (w: white, k: black, etc.)
    \t\033[4mbool\033[0m\t\033[1mlabel=y\033[0m [y/n] if y, add subplot labels (only in grid mode)
    \t\033[4mstring\033[0m\t\033[1mlabelfont=\033[0m label font family
    \t\033[4mstring\033[0m\t\033[1mlabelcolor=k\033[0m label color
    \t\033[4mstring\033[0m\t\033[1mlabelfat=normal\033[0m label font weight: normal, bold, light, etc.
    \t\033[4mstring\033[0m\t\033[1mloc="north west"\033[0m label location: north west, north east, south west, south east, north, south, west, east
    \t\033[4mstring\033[0m\t\033[1mlabeltype=a\033[0m label type: a (alphabet), # (number), I (roman)
    \t\033[4mstring\033[0m\t\033[1mlabelformat="(%s)"\033[0m label format, e.g., (%s), %s, Fig.%s, etc.
    \t\033[4mfloat\033[0m\t\033[1mlabelsz=12.\033[0m label font size
    \t\033[4mint\033[0m\t\033[1mlabelmargin=10\033[0m margin between label and subplot in pixels
    \t\033[4mstring\033[0m\t\033[1mgridorder=c\033[0m Grid order (column first:c, row first:r)
    \t\033[4mstring\033[0m\t\033[1mha=left\033[0m Horizontal alignment for grid mode
    \t\033[4mstring\033[0m\t\033[1mva=top\033[0m Vertical alignment for grid mode
    
\033[1mMORE INFO\033[0m
    \tAuthor:\tauthor_label
    \tEmail:\temail_label
    \tSource:\tgithub_label
\033[1mVERSION\033[0m
    \tversion_label
"""


import math, sys, os, subprocess, re
from textwrap import dedent
from rsfpy.utils import _str_match_re
from rsfpy.version import __version__, __email__, __author__, __github__, __SVG_SPLITTER, __SVG_BGRECT_ID


__progname__ = os.path.basename(sys.argv[0])
__doc__ = __doc__.replace("author_label",__author__)
__doc__ = __doc__.replace("email_label",__email__)
__doc__ = __doc__.replace("github_label",__github__)
__doc__ = __doc__.replace("version_label",__version__)
DOC = dedent(__doc__.replace('Msvgpen.py', __progname__))
VERB = True

try:
    from lxml import etree
except ImportError:
    print(f"lxml is required for rsfsvgpen. Please install it via 'pip install lxml'.", file=sys.stderr)
    exit(1)

def main():
    

    if len(sys.argv) < 2 and sys.stdin.isatty():
        subprocess.run(['less', '-R'], input=DOC.encode())
        sys.exit(1)
    par_dict = _str_match_re(sys.argv[1:])
    # Check stdin
    if sys.stdin.isatty():
        inputs = []
    else:
        inputs = [sys.stdin]
    
    for isvg in sys.argv[1:]:
        if os.path.isfile(isvg) and isvg.lower().endswith('.svg'):
            inputs.append(isvg)
    if len(inputs) < 1:
        sf_error("No input SVG files.")
    
    ncol = int(getfloat(par_dict, 'ncol', -1))
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
    order = par_dict.get('order', None)
    gridorder = par_dict.get('gridorder', 'c').lower()
    ha = par_dict.get('ha', 'left').lower()
    va = par_dict.get('va', 'top').lower()

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
    if label and mode == 'grid':
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
    try:
        if order is not None:
            order = [int(ind) for ind in order.split(',')]
            inputs = [inputs[ind] for ind in order]
    except:
        sf_warning(f"Invalid order={order}, use default input order.")

    if mode == 'grid':
        outtree = grid(inputs, ncol=ncol, nrow=nrow, stretchx=stretchx, stretchy=stretchy, bgcolor=bgcolor, label=label, loc=loc, labeltype=labeltype, labelformat=labelformat, labelmargin=labelmargin,
                       labelfont=labelfont, labelcolor=labelcolor, labelsz=labelsz, labelfat=labelfat,
                       ha=ha, va=va, gridorder=gridorder)
    elif mode == 'overlay':
        outtree = overlay(inputs, bgcolor=bgcolor)

    elif mode == 'movie':
         sys.stdout.write(movie(inputs))
         outtree = None
    else:
        sf_error(f"Error: unsupported mode={mode}.")
    if outtree is not None: outtree.write(sys.stdout.buffer, pretty_print=True, xml_declaration=True, encoding='utf-8')

def movie(inputs):
    """
    将多个 SVG 文件拼接成自定义格式，每段之间用 <!-- RSFPY_SPLIT --> 分隔。
    参数:
        inputs: List[str | IO] - SVG 文件路径或文件指针
    返回:
        str - 拼接后的 SVG 内容
    """
    splitter = __SVG_SPLITTER
    segments = []

    for item in inputs:
        if hasattr(item, "read"):  # 是文件指针
            content = item.read()
        elif isinstance(item, str):  # 是路径
            with open(item, "r", encoding="utf-8") as f:
                content = f.read()
        else:
            raise TypeError(f"Unsupported input type: {type(item)}")

        content = content.strip()
        if content:
            segments.append(content)

    return f"{splitter}\n" + f"\n\n{splitter}\n\n".join(segments)


def grid(inputs, ncol=-1, nrow=-1, stretchx=False, stretchy=True, bgcolor=None,label=False, loc="north west", labeltype="a", labelformat="(%s)", labelmargin=10,
         labelfont=None, labelcolor="#000", labelsz=12, labelfat="normal", ha="mid", va="btm", gridorder='r'):
    if ncol == -1 and nrow == -1:
        ncol, nrow = 3, -1

    total = len(inputs)
    if ncol != -1 and nrow != -1 and ncol * nrow < total:
        ncol, nrow = 3, -1

    if gridorder == 'r':
        if nrow == -1:
            nrow = math.ceil(total / ncol) if ncol != -1 else 3
        if ncol == -1:
            ncol = math.ceil(total / nrow)
    if ncol == -1:
        ncol = math.ceil(total / nrow) if nrow != -1 else 3
    if nrow == -1:
        nrow = math.ceil(total / ncol)

    current_ncol = math.ceil(total / nrow)
    current_nrow = math.ceil(total / ncol)
    parser = etree.XMLParser(huge_tree=True)
    svgs = [etree.parse(f, parser=parser).getroot() for f in inputs]

    sizes = []
    for svg in svgs:
        w = allinpx(svg.attrib.get("width", "100"))
        h = allinpx(svg.attrib.get("height", "100"))
        sizes.append((w, h))

    col_widths = [0] * (ncol if gridorder == 'r' else current_ncol)
    row_heights = [0] * (current_nrow if gridorder == 'r' else nrow)
    for idx, (w, h) in enumerate(sizes):
        row = idx % current_nrow if gridorder == 'r' else idx // current_ncol
        col = idx // current_nrow if gridorder == 'r' else idx % current_ncol
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
    root.attrib["width"] = f'{total_width}px'
    root.attrib["height"] = f'{total_height}px'

    for idx, svg in enumerate(svgs):
        row = idx % current_nrow if gridorder == 'r' else idx // current_ncol
        col = idx // current_nrow if gridorder == 'r' else idx % current_ncol
        x_offset = sum(col_widths[:col])
        y_offset = sum(row_heights[:row])
        orig_w, orig_h = sizes[idx]
        orig_scale_w = orig_w / no_pixel_unit(svg.attrib.get("width", "100"))
        orig_scale_h = orig_h / no_pixel_unit(svg.attrib.get("height", "100"))
        target_w = col_widths[col] # Grid width
        target_h = row_heights[row] # Grid height
        scale_x = target_w / orig_w if stretchx else 1
        scale_y = target_h / orig_h if stretchy else 1

        if target_w < orig_w: scale_x = target_w /orig_w
        if target_h < orig_h: scale_y = target_h /orig_h

        wpad_ingrid = target_w - orig_w
        hpad_ingrid = target_h - orig_h
        if gridorder == 'r':
            if col == current_ncol - 1:
                hpad_row = (current_nrow * current_ncol - total)
                if hpad_row <= 0: hpad_row = 0
                else: hpad_row += sum(row_heights[-hpad_row:])
                if va in ('mid', 'middle'):
                    y_offset +=  hpad_row / 2
                elif va in ('btm','bottom'):
                    y_offset += hpad_row
        else:
            if row == current_nrow - 1:
                wpad_col = (current_ncol * current_nrow - total)
                if wpad_col <= 0: wpad_col = 0
                else: wpad_col += sum(col_widths[-wpad_col:])
                if ha in ('mid', 'middle'): x_offset += wpad_col / 2
                elif ha in ('right'):
                    x_offset += wpad_col

        if ha in ('mid', 'middle'):
            x_offset += wpad_ingrid/2
        elif ha in ('right'):
            x_offset += wpad_ingrid
        if va in ('mid', 'middle'):
            y_offset += hpad_ingrid/2
        elif va in ('btm','bottom'):
            y_offset += hpad_ingrid

        g = etree.Element("g")
        clean_fill_recursive(svg)
        for child in svg:
            g.append(child)
        g.attrib["transform"] = f"translate({x_offset},{y_offset}) scale({orig_scale_w * scale_x},{orig_scale_h * scale_y}) "
        g.attrib["vector-effect"] = f"non-scaling-stroke"
        g.attrib["width"] = f'{target_w}px'
        g.attrib["height"] = f'{target_h}px'
        root.append(g)
        
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
            margin = labelmargin

            if loc == "north west":
                offset_x += margin
                offset_y += labelsz
            elif loc == "north east":
                offset_x += (target_w - margin)
                offset_y += labelsz
            elif loc == "south west":
                offset_x += margin
                offset_y += (target_h - labelsz/2)
            elif loc == "south east":
                offset_x += (target_w - margin)
                offset_y += (target_h - labelsz/2)
            elif loc == "north":
                offset_x += target_w / 2
                offset_y += margin
            elif loc == "south":
                offset_x += target_w / 2
                offset_y += (target_h - labelsz/2)
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
                "font-family": "DejaVu Sans" if labelfont is None else labelfont,
                "font-weight": labelfat,
                "font-size": f'{labelsz}px',
                "text-anchor": "start" if "west" in loc else ("end" if "east" in loc else "middle"),
            })
            if labelfont:
                text.attrib["font-family"] = labelfont

            text.text = label_text
            root.append(text)


    if bgcolor is not None:
        bg_rect = etree.Element("rect", {
            "x": "0",
            "y": "0",
            "width": f'{total_width}px',
            "height": f'{total_height}px',
            "fill": bgcolor,
            "id": __SVG_BGRECT_ID
        })
        root.insert(0, bg_rect)

    return etree.ElementTree(root)

def overlay(inputs, bgcolor=None):
    parser = etree.XMLParser(huge_tree=True)
    svgs = [etree.parse(f, parser=parser).getroot() for f in inputs]
    SVG_NS = "http://www.w3.org/2000/svg"
    root = etree.Element("{%s}svg" % SVG_NS, nsmap={None: SVG_NS})
    max_w = 0
    max_h = 0
    for svg in svgs:
        w = allinpx(svg.attrib.get("width", "100"))
        h = allinpx(svg.attrib.get("height", "100"))
        max_w = max(max_w, w)
        max_h = max(max_h, h)
    root.attrib["width"] = f'{max_w}px'
    root.attrib["height"] = f'{max_h}px'
    for svg in svgs:
        clean_fill_recursive(svg)
        root.append(svg)
    if bgcolor is not None:
        bg_rect = etree.Element("rect", {
            "x": "0",
            "y": "0",
            "width": f'{max_w}px',
            "height": f'{max_h}px',
            "fill": bgcolor,
            "id": __SVG_BGRECT_ID
        })
        root.insert(0, bg_rect)
    return etree.ElementTree(root)


def clean_fill_recursive(elem):
    elem_id = elem.attrib.get("id", "")
    if elem_id == __SVG_BGRECT_ID:
        parent = elem.getparent()
        if parent is not None:
            parent.remove(elem)
        return
    if elem_id.startswith("patch_1") or elem_id.startswith("patch_2"):
        for child in elem:
            if 'fill' in child.attrib.get('style', ''):
                styles = child.attrib['style'].split(';')
                styles = [s for s in styles if not s.strip().startswith('fill:')]
                styles.append('fill: none')
                child.attrib['style'] = ';'.join(styles)
    for child in list(elem):
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
    if args[-1] is not None and isinstance(args[-1], str):
        if args[-1].endswith('.'): endl = '\n' 
        if args[-1].endswith(';'): endl = '\r'
    endl = kwargs.pop('end', endl)
    if verb: print(f'{__progname__}:', *args, file=file, end=endl, **kwargs)


def sf_error(*args, **kwargs):
    sf_warning(*args, verb=True, **kwargs)
    sys.exit(1)

def allinpx(s: str) -> float:
    """
    将带单位的长度字符串转换为像素(px)数值。
    支持: px, pt, pc, in, cm, mm, em, ex, % (部分相对单位需上下文)
    """
    s = s.strip().lower()
    if s.endswith('px'):
        return float(s.replace('px', ''))
    elif s.endswith('pt'):
        # 1pt = 1/72 in, 1in = 96px → 1pt = 96/72 = 1.333...px
        return float(s.replace('pt', '')) * 96.0 / 72.0
    elif s.endswith('pc'):
        # 1pc = 12pt = 16px
        return float(s.replace('pc', '')) * 16.0
    elif s.endswith('in'):
        # 1in = 96px
        return float(s.replace('in', '')) * 96.0
    elif s.endswith('cm'):
        # 1in = 2.54cm → 1cm = 96/2.54 px
        return float(s.replace('cm', '')) * 96.0 / 2.54
    elif s.endswith('mm'):
        # 1cm = 10mm
        return float(s.replace('mm', '')) * 96.0 / 25.4
    elif s.endswith('em'):
        # 1em = 当前字体大小 (假设16px)
        return float(s.replace('em', '')) * 16.0
    elif s.endswith('ex'):
        # 1ex ≈ 0.5em (粗略估计)
        return float(s.replace('ex', '')) * 8.0
    elif s.endswith('%'):
        # 百分比需要上下文，这里先返回原始数值
        return float(s.replace('%', ''))  # 需结合父元素宽度解释
    else:
        # 默认当作 px
        return float(s)


def no_pixel_unit(s: str) -> str:
    """
    去掉长度字符串里的单位，只保留数值部分（含正负号、小数点、科学计数法 e/E）。
    """
    s = s.strip()
    # 用正则保留数字、正负号、小数点、e/E
    cleaned = re.sub(r'[^0-9eE\+\-\.]', '', s)
    return float(cleaned)

if __name__ == "__main__":
    main()