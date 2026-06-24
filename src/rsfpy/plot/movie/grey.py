# -*- coding: utf-8 -*-
"""SVG image replacement for grey movie frames."""

from .base import MovieTemplate, MovieUpdater
from rsfpy.plot import arr2png, prepare_svg_template, replace_png
from rsfpy.plot.style import parse_float


class GreyMovieUpdater(MovieUpdater):
    """Keep the first SVG's vector decoration and replace only its image data."""

    mode = "grey"

    def build_template(self, svg, context=None):
        pieces = prepare_svg_template(svg)
        return MovieTemplate(svg=svg, mode=self.mode, metadata={"pieces": pieces})

    def update_frame(self, template, frame, context=None):
        params = context.params if context is not None else {}
        png = arr2png(frame.payload,
                      clip=frame.clip, pclip=parse_float(params.get("pclip"), 99.0), bias=frame.bias,
                      allpos=frame.allpos, cmap=frame.cmap,
                      gain=frame.gain,
                      max_pixels=parse_float(params.get("maxpixels")),
                      min1=parse_float(params.get("min1")), max1=parse_float(params.get("max1")),
                      min2=parse_float(params.get("min2")), max2=parse_float(params.get("max2")),
                      cords1=frame.payload.axis1, cords2=frame.payload.axis2,
                      dpi=frame.dpi)
        pieces = template.metadata["pieces"]
        template.svg = replace_png(*pieces[:3], new_b64=png)
        return template
