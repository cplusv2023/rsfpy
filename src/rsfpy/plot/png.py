import struct, zlib, base64, re
import numpy as np
from matplotlib import cm, colors
import xml.etree.ElementTree as ET
from ..version import __AX1_NAME

def make_chunk(chunk_type: bytes, data: bytes) -> bytes:
    """Create PNG chunk"""
    length = struct.pack("!I", len(data))
    crc = struct.pack("!I", zlib.crc32(chunk_type + data) & 0xffffffff)
    return length + chunk_type + data + crc


def arr2png(arr: np.ndarray, clip=None, pclip=None, bias=0, allpos=False, cmap: str = "viridis", dpi: int = 100,
            min1=None, max1=None, min2=None, max2=None, cords1=None, cords2=None) -> str:
    """
    Transform a grayscale/color array to PNG and return StringIO
    - Grayscale (H, W), float or uint8
    - RGB       (H, W, 3)
    - RGBA      (H, W, 4)
    """
    arr = np.asarray(arr)
    h, w = arr.shape[0], arr.shape[1]

    if cords1 is None:
        cords1 = np.arange(h)
    if cords2 is None:
        cords2 = np.arange(w)

    if min1 is None: min1 = cords1[0]
    if max1 is None: max1 = cords1[-1]
    if min2 is None: min2 = cords2[0]
    if max2 is None: max2 = cords2[-1]

    if min1 > max1: min1, max1 = max1, min1
    if min2 > max2: min2, max2 = max2, min2

    i1 = np.searchsorted(cords1, min1, side="left")
    i2 = np.searchsorted(cords1, max1, side="right")
    j1 = np.searchsorted(cords2, min2, side="left")
    j2 = np.searchsorted(cords2, max2, side="right")

    arr = arr[i1:i2, j1:j2]

    # --- Grey-scale array ---
    if arr.ndim == 2 or (arr.ndim == 3 and arr.shape[2] == 1):
        if arr.dtype == np.uint8:
            normed = arr.astype(float) / 255.0
        else:
            vmin, vmax = clip2val(arr, clip=clip, bias=bias, pclip=pclip, allpos=allpos)
            norm = colors.Normalize(vmin=vmin, vmax=vmax, clip=True)
            normed = norm(arr)
        rgba = cm.get_cmap(cmap)(normed, bytes=True)  # (H, W, 4)
        data = rgba
        color_type = 6  # RGBA
    # --- RGB array ---
    elif arr.ndim == 3 and arr.shape[2] == 3:
        if arr.dtype != np.uint8:
            arr = np.clip(arr * 255, 0, 255).astype(np.uint8)
        data = arr
        color_type = 2  # RGB
    # --- RGBA array ---
    elif arr.ndim == 3 and arr.shape[2] == 4:
        if arr.dtype != np.uint8:
            arr = np.clip(arr * 255, 0, 255).astype(np.uint8)
        data = arr
        color_type = 6  # RGBA
    else:
        raise ValueError("Input must be grayscale (H,W), RGB (H,W,3) or RGBA (H,W,4)")

    h, w = data.shape[0], data.shape[1]
    depth = 8  

    # --- PNG Header ---
    png_sig = b"\x89PNG\r\n\x1a\n"

    # --- IHDR ---
    ihdr = struct.pack("!IIBBBBB", w, h, depth, color_type, 0, 0, 0)
    ihdr_chunk = make_chunk(b"IHDR", ihdr)

    # --- Image data, with filter=0 added to the beginning of each row ---
    raw_data = b"".join([b"\x00" + data[y].tobytes() for y in range(h)])
    compressed = zlib.compress(raw_data)
    idat_chunk = make_chunk(b"IDAT", compressed)

    # --- IEND ---
    iend_chunk = make_chunk(b"IEND", b"")

    # --- Concatenate PNG ---
    chunks = png_sig + ihdr_chunk + idat_chunk + iend_chunk
    b64 = base64.b64encode(chunks).decode("ascii")
    return b64

def prepare_svg_template(svg: str, isvg: int = 0):
    pattern = re.compile(r'<image\b[^>]*xlink:href="data:image/png;base64,[^"]*"[^>]*/?>')
    matches = list(pattern.finditer(svg))
    if isvg >= len(matches):
        raise ValueError(f"Cannot find the {isvg}-th PNG <image> tag in SVG.")

    match = matches[isvg]
    tag_str = match.group(0)

    prefix = svg[:match.start()]
    suffix = svg[match.end():]

    # 去掉 <image 和 />，只保留属性部分
    inner = tag_str[len("<image"):].strip()
    if inner.endswith("/>"):
        inner = inner[:-2].strip()
    elif inner.endswith(">"):
        inner = inner[:-1].strip()

    # 提取属性
    attr_pattern = re.compile(r'(\S+)\s*=\s*"([^"]*)"')
    attrs = dict(attr_pattern.findall(inner))
    attrs.pop("xlink:href", None)

    return prefix, suffix, attrs




def rebuild_image_tag(pnglabel: dict, new_b64: str) -> str:
    """Rebuild <image> tag from attributes and new base64 data"""
    attrs = []
    for k, v in pnglabel.items():
        attrs.append(f'{k}="{v}"')
    return '<image ' + f' xlink:href="data:image/png;base64,{new_b64}" ' + " ".join(attrs) + '/>'

def replace_png(prefix: str, suffix: str, pnglabel: dict, new_b64: str,
                shear=False,
                point1=None, point2=None, which: str = 'x') -> str:
    """
    Replace PNG image in SVG content and apply shear transform.
    """
    newdict = pnglabel.copy()
    if shear:
        ww = float(pnglabel.get("width", "0"))
        hh = float(pnglabel.get("height", "0"))
        th = hh / ww 
        matrix = ""

        if which == 'x':
            k =  1 / th * (1-point2)
            x0 = float(pnglabel.get("x", "0"))
            y0 = float(pnglabel.get("y", "0"))
            e = - k * y0
            f = 0
            matrix = f"matrix(1 0 {k:.6f} 1 {e:.6f} {f:.6f})  "
            newdict.update({'width': f"{ww * (1 - point2):.6f}"})

        elif which == 'y':
            k = th * (1 -point1)
            x0 = float(pnglabel.get("x", "0"))
            y0 = float(pnglabel.get("y", "0"))
            e = 0
            f = - (k * x0)
            matrix = f"matrix(1 {k:.6f} 0 1 {e:.6f} {f:.6f})"
            newdict.update({'height': f"{hh * ( point1):.6f}"})


        if "transform" in newdict:
            newdict["transform"] = newdict["transform"] + " " + matrix
        else:
            newdict["transform"] = matrix

    newdict["preserveAspectRatio"] = "none"

    image_tag = rebuild_image_tag(newdict, new_b64)

    return prefix + image_tag + suffix



def clip2val(arr, clip=None, bias=0, pclip=None, allpos=False):
    if bias is None:
        bias = 0

    if pclip is not None and (clip is None) :
        if hasattr(arr, "pclip"):
            clip = arr.pclip(pclip)
        else:
            clip = np.percentile(np.abs(arr), pclip)
        if allpos:
            vmin, vmax = 0, clip
        else:
            vmin, vmax = bias - clip, bias + clip

    if clip is not None:
        clip = abs(clip)
        if allpos:
            vmin, vmax = 0, clip
        else:
            vmin, vmax = bias - clip, bias + clip

    elif vmin is not None or vmax is not None:
        if vmin is None and vmax is not None:
            vmin = -vmax
        elif vmax is None and vmin is not None:
            vmax = -vmin
        if vmin > vmax:
            vmin, vmax = vmax, vmin
    return vmin, vmax


def extract_ax_info(svg_str, prefix=__AX1_NAME.split("%")[0]):

    group_pattern = re.compile(
        r'<g\s+id="' + re.escape(prefix) +
        r'([\d\.\-eE]+)_([\d\.\-eE]+)_([\d\.\-eE]+)_([\d\.\-eE]+)".*?>'
        r'.*?<path[^>]+d="M\s+([\d\.\-eE]+)\s+([\d\.\-eE]+)\s+'
        r'L\s+([\d\.\-eE]+)\s+([\d\.\-eE]+)\s+'
        r'L\s+([\d\.\-eE]+)\s+([\d\.\-eE]+)\s+'
        r'L\s+([\d\.\-eE]+)\s+([\d\.\-eE]+)',
        re.MULTILINE | re.DOTALL
    )

    match = group_pattern.search(svg_str)
    if not match:
        return None

    miny, maxy, minx, maxx = map(float, match.group(1,2,3,4))
    coords = list(map(float, match.groups()[4:]))
    xs, ys = coords[0::2], coords[1::2]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)

    return {
        "data_range": (minx, maxx, miny, maxy),
        "svg_rect": (x0, x1, y0, y1)
    }



def set_line(line_prefix1, line_prefix2):
    cache = {}

    def set_line_inner(lines, cords1, cords2):
        nonlocal cache
        if "index1" not in cache:
            for i, line in enumerate(lines):
                if f'id="{line_prefix1}"' in line:
                    pattern = re.compile(
                        r'^(.*?<g\s+id="' + re.escape(line_prefix1) +
                        r'".*?<path[^>]*d=")([^"]*)(".*)$',
                        re.MULTILINE | re.DOTALL
                    )
                    m = pattern.search(line)
                    if not m:
                        raise ValueError(f"Cannot find <path> with id={line_prefix1}")
                    prefix_all, d_content, suffix = m.groups()

                    tokens = d_content.replace("\n", " ").split()
                    if len(tokens) < 6 or tokens[0] != "M" or tokens[3] != "L":
                        raise ValueError(f"Unexpected <path> format: {d_content}")
                    orig_x0, orig_y0 = float(tokens[1]), float(tokens[2])
                    orig_x1, orig_y1 = float(tokens[4]), float(tokens[5])

                    cache = {
                        "index1": i,
                        "prefix1": prefix_all,
                        "suffix1": suffix,
                        "coords1": [orig_x0, orig_x1, orig_y0, orig_y1]
                    }
                    break
            else:
                raise ValueError(f"Cannot find id={line_prefix1} in lines")
        if cache["index1"] == 1:
            lines1 = [lines[0], cache["prefix1"], cache["suffix1"]]
        else:
            lines1 = [cache["prefix1"], cache["suffix1"], lines[1]]
        if "index2" not in cache:
            for j, line in enumerate(lines1):
                if f'id="{line_prefix2}"' in line:
                    pattern = re.compile(
                        r'^(.*?<g\s+id="' + re.escape(line_prefix2) +
                        r'".*?<path[^>]*d=")([^"]*)(".*)$',
                        re.MULTILINE | re.DOTALL
                    )
                    m = pattern.search(line)
                    if not m:
                        raise ValueError(f"Cannot find <path> with id={line_prefix2} in {line[:200]}")
                    prefix_all, d_content, suffix = m.groups()
                    tokens = d_content.replace("\n", " ").split()
                    if len(tokens) < 6 or tokens[0] != "M" or tokens[3] != "L":
                        raise ValueError(f"Unexpected <path> format: {d_content}")
                    orig_x0, orig_y0 = float(tokens[1]), float(tokens[2])
                    orig_x1, orig_y1 = float(tokens[4]), float(tokens[5])

                    cache["index2"] = j
                    cache["prefix2"] = prefix_all
                    cache["suffix2"] = suffix
                    cache["coords2"] = [orig_x0, orig_x1, orig_y0, orig_y1]
                    break
            else:
                raise ValueError(f"Cannot find id={line_prefix2} in lines")

        lines1.pop(cache["index2"])
        lines1.insert(cache["index2"], cache["suffix2"])
        lines1.insert(cache["index2"], cache["prefix2"])
        

        x0, x1, y0, y1 = cords1
        xx0, xx1, yy0, yy1 = cords2



        new_x0 = cache["coords1"][0] if x0 is None else x0
        new_x1 = cache["coords1"][1] if x1 is None else x1
        new_y0 = cache["coords1"][2] if y0 is None else y0
        new_y1 = cache["coords1"][3] if y1 is None else y1

        new_xx0 = cache["coords2"][0] if xx0 is None else xx0
        new_xx1 = cache["coords2"][1] if xx1 is None else xx1
        new_yy0 = cache["coords2"][2] if yy0 is None else yy0
        new_yy1 = cache["coords2"][3] if yy1 is None else yy1

        cache["coords1"] = [new_x0, new_x1, new_y0, new_y1]
        cache["coords2"] = [new_xx0, new_xx1, new_yy0, new_yy1]

        new_d1 = f'M {new_x0:.6f} {new_y0:.6f} L {new_x1:.6f} {new_y1:.6f}'
        new_d2 = f'M {new_xx0:.6f} {new_yy0:.6f} L {new_xx1:.6f} {new_yy1:.6f}'

        new_lines = list(lines1)
        new_lines[cache["index2"]] = f'{new_lines[cache["index2"]]}{new_d2}{new_lines[cache["index2"]+1]}'
        new_lines.pop(cache["index2"] + 1)
        new_lines[cache["index1"]] = f'{new_lines[cache["index1"]]}{new_d1}{new_lines[cache["index1"]+1]}'
        new_lines.pop(cache["index1"] + 1)
        return new_lines

    return set_line_inner



def set_text(text_prefix, lines, new_text=None, x0=None, y0=None):
    if hasattr(set_text, "cache") is False:
        set_text.cache = {}

        for i, line in enumerate(lines):
            if f'id="{text_prefix}"' in line:
                pattern = re.compile(
                    r'^(.*?)'                                   # before
                    r'(<g\s+id="' + re.escape(text_prefix) +    # prefix 开头
                    r'".*?>\s*<text[^>]*?)'
                    r'(?:x="([\-0-9\.eE]+)"\s*)?'               # x_str 可选
                    r'(?:y="([\-0-9\.eE]+)"\s*)?'               # y_str 可选
                    r'transform="([^"]+)"'                      # transform
                    r'([^>]*)>'                                 # others
                    r'(.*?)'                                    # old_text
                    r'(</text>.*?</g>)'                         # suffix (非贪婪，停在第一个 </g>)
                    r'(.*)$',                                   # after
                    re.MULTILINE | re.DOTALL
                )

                m = pattern.search(line)
                if not m:
                    raise ValueError(f"未找到 id={text_prefix} 的 <text>")
                before, prefix, x_str, y_str, transform, others, old_text, suffix, after = m.groups()

                x_cord = float(x_str) if x_str is not None else None
                y_cord = float(y_str) if y_str is not None else None

                theta = None
                m_rot = re.match(r'rotate\(\s*([\-0-9\.eE]+)\s+[\-0-9\.eE]+\s+[\-0-9\.eE]+\s*\)', transform)
                if m_rot and len(m_rot.groups()) == 3:
                    theta = m_rot.group(1)
                    orig_x, orig_y = float(m_rot.group(2)), float(m_rot.group(3))
                else:
                    m_trans = re.match(
                        r'translate\(\s*([\-0-9\.eE]+)\s+([\-0-9\.eE]+)\s*\)\s*rotate\(\s*([\-0-9\.eE]+)\s*\)',
                        transform
                    )
                    if m_trans:
                        orig_x, orig_y, theta = float(m_trans.group(1)), float(m_trans.group(2)), m_trans.group(3)
                    else:
                        orig_x, orig_y, theta = 0.0, 0.0, "0"


                break
        else:
            raise ValueError(f"未找到 id={text_prefix} 的行")

        if x_cord is not None:
            orig_x = x_cord
        if y_cord is not None:
            orig_y = y_cord

        # 如果传入 None，就保留原值
        new_x = orig_x
        new_y = orig_y
        set_text.cache = {
            "prefix": prefix,
            "others": others,
            "suffix": suffix,
            "theta": theta,
            "new_x": new_x,
            "new_y": new_y,
            "final_text": old_text if new_text is None else new_text,
        }
    final_text = set_text.cache["final_text"] if new_text is None else new_text
    new_x = set_text.cache["new_x"] if x0 is None else x0
    new_y = set_text.cache["new_y"] if y0 is None else y0
    theta = set_text.cache["theta"]
    others = set_text.cache["others"]
    prefix = set_text.cache["prefix"]
    suffix = set_text.cache["suffix"]
    
    new_transform = f'translate({new_x:.6f} {new_y:.6f}) rotate({theta})'
    new_line = f'{prefix} transform="{new_transform}" {others}>{final_text}{suffix}'

    if set_text.cache.get("endline",None) is None:
        new_lines = list(lines)
        new_lines[i] = before + after
        set_text.cache["begline"] = new_lines[0]
        set_text.cache["endline"] = new_lines[-1].split("</svg>")[0]
    else:
        new_lines = [set_text.cache["begline"], set_text.cache["endline"]]
    new_lines[-1] = set_text.cache["endline"] + new_line + "\n</svg>"
    return new_lines
