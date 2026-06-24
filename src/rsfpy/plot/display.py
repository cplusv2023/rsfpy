# -*- coding: utf-8 -*-
"""Shared image display rules for Madagascar-compatible plot commands."""

from dataclasses import dataclass
import math

import numpy as np
from matplotlib import colors


DEFAULT_MAX_IMAGE_PIXELS = 4_000_000


def make_colormap(value):
    """Resolve Madagascar shortcuts, Matplotlib names, or ``red,#000,blue``."""

    from matplotlib.colors import LinearSegmentedColormap, is_color_like

    aliases = {
        "j": "jet", "J": "jet_r", "i": "gray", "I": "gray",
        "s": "seismic", "S": "seismic_r",
        "kwr": ("k", "w", "r"), "rwk": ("r", "w", "k"),
    }
    mapped = aliases.get(value, value)
    if isinstance(mapped, tuple):
        return LinearSegmentedColormap.from_list("rsfpy_colormap", mapped)
    if isinstance(mapped, str) and "," in mapped:
        values = [item.strip() for item in mapped.split(",") if item.strip()]
        if len(values) >= 2 and all(is_color_like(item) for item in values):
            return LinearSegmentedColormap.from_list("rsfpy_colormap", values)
    return mapped


def downsample_image(array, max_pixels=DEFAULT_MAX_IMAGE_PIXELS):
    """Decimate a raster for display while preserving its outer extent."""

    array = np.asarray(array)
    if max_pixels is None:
        max_pixels = DEFAULT_MAX_IMAGE_PIXELS
    try:
        max_pixels = float(max_pixels)
    except (TypeError, ValueError):
        max_pixels = DEFAULT_MAX_IMAGE_PIXELS
    if array.ndim < 2 or max_pixels <= 0:
        return array
    height, width = array.shape[:2]
    if height * width <= max_pixels:
        return array
    factor = int(math.ceil(math.sqrt((height * width) / float(max_pixels))))
    return array[::factor, ::factor, ...]


@dataclass(frozen=True)
class Gain:
    clip: float
    bias: float = 0.0
    allpos: bool = False
    gpow: float = 1.0
    polarity: bool = False

    @property
    def vmin(self):
        return 0.0 if self.allpos else self.bias - self.clip

    @property
    def vmax(self):
        return self.clip if self.allpos else self.bias + self.clip

    def norm(self):
        return MadagascarNormalize(self)


class MadagascarNormalize(colors.Normalize):
    """Linear/power mapping equivalent to grey.c's clip/bias color-table step."""

    def __init__(self, gain):
        super().__init__(vmin=gain.vmin, vmax=gain.vmax, clip=True)
        self.gain = gain

    def __call__(self, value, clip=None):
        values = np.ma.asarray(value, dtype=float)
        if self.gain.allpos:
            normed = np.ma.clip(values / self.gain.clip, 0.0, 1.0)
        else:
            normed = np.ma.clip((values - self.gain.bias) / self.gain.clip, -1.0, 1.0)
            normed = (np.sign(normed) * np.power(np.abs(normed), self.gain.gpow) + 1.0) / 2.0
        if self.gain.allpos and self.gain.gpow != 1.0:
            normed = np.power(normed, self.gain.gpow)
        if self.gain.polarity:
            normed = 1.0 - normed
        return normed


def estimate_gain(data, *, clip=None, pclip=99.0, bias=0.0, mean=False,
                  allpos=False, gpow=1.0, polarity=False):
    """Estimate grey.c-style display gain from one reference panel or panel set."""

    values = np.asarray(data, dtype=float).ravel()
    values = values[np.isfinite(values)]
    if values.size == 0:
        return Gain(clip=np.finfo(float).eps, bias=bias or 0.0, allpos=allpos,
                    gpow=max(float(gpow), 1.0), polarity=polarity)
    if mean:
        bias = float(np.mean(values))
    elif bias is None:
        bias = 0.0
    if clip is None:
        pclip = min(100.0, max(np.finfo(float).eps, float(pclip)))
        reference = values if allpos else values - bias
        clip = float(np.percentile(np.abs(reference), pclip))
    clip = max(abs(float(clip)), np.finfo(float).eps)
    gpow = float(gpow)
    if gpow <= 0:
        gpow = 1.0
    return Gain(clip=clip, bias=float(bias), allpos=allpos, gpow=gpow, polarity=polarity)
