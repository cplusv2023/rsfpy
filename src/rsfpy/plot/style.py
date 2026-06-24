# -*- coding: utf-8 -*-
"""Shared plotting style aliases.

The command-line tools should accept both Madagascar-style names such as
``framefat`` and matplotlib-style names such as ``linewidth``.  This module
keeps that compatibility in one place.
"""

from dataclasses import dataclass
from typing import Optional


COLOR_ALIASES = {
    "black": "#000000",
    "white": "#ffffff",
    "red": "#ff0000",
    "green": "#00ff00",
    "blue": "#0000ff",
    "yellow": "#ffff00",
    "cyan": "#00ffff",
    "magenta": "#ff00ff",
    "gray": "#808080",
    "grey": "#808080",
    "k": "#000000",
    "w": "#ffffff",
    "r": "#ff0000",
    "g": "#00ff00",
    "b": "#0000ff",
    "y": "#ffff00",
    "c": "#00ffff",
    "m": "#ff00ff",
}

FONT_WEIGHT_ALIASES = {
    "ultralight": 100,
    "light": 200,
    "book": 300,
    "normal": 400,
    "regular": 400,
    "medium": 500,
    "semibold": 600,
    "demibold": 600,
    "demi": 600,
    "bold": 700,
    "heavy": 800,
    "black": 900,
}

FONT_WEIGHT_NAMES = {
    100: "ultralight",
    200: "light",
    300: "book",
    400: "normal",
    500: "medium",
    600: "semibold",
    700: "bold",
    800: "heavy",
    900: "black",
}


@dataclass(frozen=True)
class ElementStyle:
    """Canonical element style values used by RSFPY plotting commands."""

    color: Optional[str] = None
    width: Optional[float] = None
    fontsize: Optional[float] = None
    fontweight: Optional[str] = None
    fontfamily: Optional[str] = None


def first_value(params, *names, default=None):
    for name in names:
        if name in params and params[name] not in (None, ""):
            return params[name]
    return default


def parse_float(value, default=None):
    if value in (None, ""):
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def normalize_color(value, default=None):
    if value in (None, ""):
        return default
    key = str(value).strip().lower()
    return COLOR_ALIASES.get(key, value)


def normalize_fontweight(value, default=None):
    if value in (None, ""):
        return default
    key = str(value).strip().lower()
    if key in FONT_WEIGHT_ALIASES:
        return FONT_WEIGHT_NAMES[FONT_WEIGHT_ALIASES[key]]
    try:
        num = float(key)
    except ValueError:
        return str(value)
    rounded = int(((num + 99) // 100) * 100)
    rounded = min(max(rounded, 100), 900)
    return FONT_WEIGHT_NAMES[rounded]


def element_style(params, prefix="", fallback_prefixes=(), text=False):
    """Return a canonical style from compatible RSF/matplotlib aliases.

    For a line prefix such as ``frame`` this accepts ``framecolor/framecol``
    and ``framewidth/framefat``.  For text prefixes such as ``label``, pass
    ``text=True`` so ``labelfat`` maps to font weight instead of line width.
    """

    prefixes = (prefix,) + tuple(fallback_prefixes)
    color_names = []
    width_names = []
    size_names = []
    weight_names = []
    family_names = []

    for item in prefixes:
        color_names.extend([item + "color", item + "col"])
        if text:
            width_names.extend([item + "width", item + "linewidth"])
            size_names.extend([item + "sz", item + "size", item + "fontsize"])
            weight_names.extend([item + "weight", item + "fat", item + "fontweight"])
            family_names.extend([item + "family", item + "font", item + "fontfamily"])
        else:
            width_names.extend([item + "width", item + "fat", item + "linewidth", item + "linefat"])

    if not prefix:
        color_names.extend(["color", "col"])
        if text:
            width_names.extend(["width", "linewidth"])
            size_names.extend(["sz", "size", "fontsize"])
            weight_names.extend(["weight", "fontweight", "fontfat", "fat"])
            family_names.extend(["family", "font", "fontfamily"])
        else:
            width_names.extend(["width", "fat", "linewidth", "linefat"])

    return ElementStyle(
        color=normalize_color(first_value(params, *color_names)),
        width=parse_float(first_value(params, *width_names)),
        fontsize=parse_float(first_value(params, *size_names)),
        fontweight=normalize_fontweight(first_value(params, *weight_names)),
        fontfamily=first_value(params, *family_names),
    )
