# -*- coding: utf-8 -*-
"""SVG image and indicator updates for grey3 movie frames."""

from .base import MovieTemplate, MovieUpdater
from rsfpy.plot import arr2png, extract_ax_info, prepare_svg_template, replace_png, set_line, set_text
from rsfpy.version import (
    __AX1_HLINE_NAME as AX1_HLINE_NAME, __AX1_NAME as AX1_NAME,
    __AX1_VLINE_NAME as AX1_VLINE_NAME, __AX2_HLINE_NAME as AX2_HLINE_NAME,
    __AX2_NAME as AX2_NAME, __AX2_VLINE_NAME as AX2_VLINE_NAME,
    __AX3_HLINE_NAME as AX3_HLINE_NAME, __AX3_NAME as AX3_NAME,
    __AX3_VLINE_NAME as AX3_VLINE_NAME, __FRAME1_LABEL_NAME as FRAME1_LABEL_NAME,
    __FRAME2_LABEL_NAME as FRAME2_LABEL_NAME, __FRAME3_LABEL_NAME as FRAME3_LABEL_NAME,
)


class Grey3MovieUpdater(MovieUpdater):
    """Update one grey3 slice and its frame indicator without redrawing Matplotlib."""

    mode = "grey3"

    def build_template(self, svg, movie, isflat):
        index = [2, 1, 0][movie - 1] if isflat else [1, 2, 0][movie - 1]
        return MovieTemplate(svg=svg, mode=self.mode,
                             metadata={"parts": prepare_svg_template(svg, index), "movie": movie, "isflat": isflat})

    def update_frame(self, template, frame, context=None):
        data = frame.payload
        movie = template.metadata["movie"]
        isflat = template.metadata["isflat"]
        frame1, frame2, frame3 = frame.frame1, frame.frame2, frame.frame3
        if movie == 1:
            image = data.window(n1=1, f1=frame1, copy=False).T
        elif movie == 2:
            image = data.window(n2=1, f2=frame2, copy=False)[::-1, :]
        else:
            image = data.window(n3=1, f3=frame3, copy=False)[::-1, :]
        png = arr2png(image, clip=frame.clip, pclip=frame.pclip, bias=frame.bias,
                      allpos=frame.allpos, cmap=frame.cmap, dpi=frame.dpi, gain=frame.gain,
                      max_pixels=context.params.get("maxpixels") if context is not None else None)
        parts = template.metadata["parts"]
        header = parts[0] + parts[1]
        ax1 = extract_ax_info(header, prefix=AX1_NAME.split("%")[0])
        ax2 = extract_ax_info(header, prefix=AX2_NAME.split("%")[0])
        ax3 = extract_ax_info(header, prefix=AX3_NAME.split("%")[0])
        contents = parts[:2]
        if ax1 is not None and ax2 is not None and ax3 is not None:
            ax1_x0, ax1_x1, ax1_y0, ax1_y1 = ax1["data_range"]
            ax1_xx0, ax1_xx1, ax1_yy0, ax1_yy1 = ax1["svg_rect"]
            ax2_x0, ax2_x1, ax2_y0, ax2_y1 = ax2["data_range"]
            ax2_xx0, ax2_xx1, ax2_yy0, ax2_yy1 = ax2["svg_rect"]
            ax3_x0, ax3_x1, ax3_y0, ax3_y1 = ax3["data_range"]
            ax3_xx0, ax3_xx1, ax3_yy0, ax3_yy1 = ax3["svg_rect"]
            if movie == 1:
                value = data.axis1[frame1]
                pos = ax1_yy0 + (value - ax1_y0) / (ax1_y1 - ax1_y0) * (ax1_yy1 - ax1_yy0)
                contents = set_text(FRAME1_LABEL_NAME, contents,
                                    str(data.axis1[frame1]), y0=pos + ax2_yy0 - ax1_yy0)
                contents = set_line(AX1_HLINE_NAME, AX2_HLINE_NAME)(
                    contents, cords1=[None, None, pos, pos],
                    cords2=[None, None, pos, pos + ax2_yy0 - ax1_yy0])
            elif movie == 2:
                value = data.axis2[frame2]
                pos = ax1_xx0 + (value - ax1_x0) / (ax1_x1 - ax1_x0) * (ax1_xx1 - ax1_xx0)
                contents = set_text(FRAME2_LABEL_NAME, contents,
                                    str(data.axis2[frame2]), x0=pos + ax3_xx1 - ax1_xx1)
                contents = set_line(AX1_VLINE_NAME, AX3_VLINE_NAME)(
                    contents, cords1=[pos, pos, None, None],
                    cords2=[pos, pos + ax3_xx1 - ax1_xx1, None, None])
            else:
                value = data.axis3[frame3]
                xpos = ax2_xx0 + (value - ax2_x0) / (ax2_x1 - ax2_x0) * (ax2_xx1 - ax2_xx0)
                ypos = ax1_yy0 - (value - ax2_x0) / (ax2_x1 - ax2_x0) * (ax1_yy0 - ax2_yy0)
                if isflat:
                    contents = set_text(FRAME3_LABEL_NAME, contents, str(value), x0=xpos, y0=ax1_yy0 * 0.95)
                    cords2 = [ax3_xx1, ax3_xx0,
                              ax1_yy0 - (value - ax3_y0) / (ax3_y1 - ax3_y0) * (ax3_yy1 - ax3_yy0),
                              ax1_yy0 - (value - ax3_y0) / (ax3_y1 - ax3_y0) * (ax3_yy1 - ax3_yy0)]
                else:
                    contents = set_text(FRAME3_LABEL_NAME, contents, str(value), x0=xpos,
                                        y0=(ax2_yy1 - ax1_yy0 + ypos) * 1.025)
                    cords2 = [ax1_xx0 + (value - ax2_x0) / (ax2_x1 - ax2_x0) * (ax2_xx1 - ax2_xx0),
                              ax3_xx1, ypos, ypos]
                contents = set_line(AX2_VLINE_NAME, AX3_HLINE_NAME)(
                    contents, cords1=[xpos, xpos, ax2_yy1 - ax1_yy0 + ypos, ypos], cords2=cords2)
        template.svg = replace_png(*contents[:2], parts[2], new_b64=png,
                                   shear=not isflat and movie in (1, 2),
                                   point1=frame.point1, point2=frame.point2,
                                   which="x" if movie == 1 else "y")
        return template
