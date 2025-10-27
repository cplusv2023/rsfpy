import struct, zlib, base64, re
import numpy as np
from matplotlib import cm, colors
import xml.etree.ElementTree as ET

def make_chunk(chunk_type: bytes, data: bytes) -> bytes:
    """Create PNG chunk"""
    length = struct.pack("!I", len(data))
    crc = struct.pack("!I", zlib.crc32(chunk_type + data) & 0xffffffff)
    return length + chunk_type + data + crc


def arr2png(arr: np.ndarray, clip=None, pclip=None, bias=None, allpos=False, cmap: str = "viridis", dpi: int = 100,
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
        th = (1 - point1) / (1 - point2) 

        matrix = ""

        if which == 'x':
            k = 1. / (th * (1 - point2)) * 1.01
            x0 = float(pnglabel.get("x", "0"))
            y0 = float(pnglabel.get("y", "0"))
            e = x0 - (k * y0 + point2 * x0)
            f = 0
            matrix = f"matrix({point2:.6f} 0 {k:.6f} 1 {e:.6f} {f:.6f})"

        elif which == 'y':
            k = th * point1 * 0.99
            x0 = float(pnglabel.get("x", "0"))
            y0 = float(pnglabel.get("y", "0"))
            e = 0
            f = y0 - (k * x0 + point1 * y0)
            matrix = f"matrix(1 {k:.6f} 0 {point1:.6f} {e:.6f} {f:.6f})"


        if "transform" in newdict:
            newdict["transform"] = newdict["transform"] + " " + matrix
        else:
            newdict["transform"] = matrix

    newdict["preserveAspectRatio"] = "none"

    image_tag = rebuild_image_tag(newdict, new_b64)

    return prefix + image_tag + suffix



def clip2val(arr, clip=None, bias=None, pclip=None, allpos=False):
    if bias is None:
        bias = 0

    if pclip is not None and (clip is None) and (vmin is None and vmax is None):
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