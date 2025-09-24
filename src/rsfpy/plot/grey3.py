

import numpy as np
import matplotlib.pyplot as plt
import warnings
import matplotlib.ticker as mticker
from typing import Optional, Union
from .grey import grey
from .wiggle import wiggle
import matplotlib.transforms as transforms
# from mpl_toolkits.axisartist.floating_axes import \
    # GridHelperCurveLinear
# from mpl_toolkits.axisartist.grid_finder import MaxNLocator
import mpl_toolkits.axisartist as artist
from matplotlib.ticker import MaxNLocator as MaxNLocator1


class Grey3Attributes:
    '''
     Feel so hard to manage all details.
     This is for all the chaos
    '''
    fig = None
    # Axes
    main_ax = None  # The base ax
    ax1 = None      # First ax
    ax2 = None      # Second ax
    ax3 = None      # Third ax
    cax = None      # Colorbar ax (can be None)
    title_ax = None # Title ax

    # Graph elements
    im1 = None
    im2 = None
    im3 = None
    cbar = None

    # Others
    title_text = None
    vlines = []
    hlines = []
    ticklabels = []
    labels = []

    def set_ticklabels(self, **tick_kwargs):
        labels = []
        labels += self.ticklabels

        for ilabel in labels:
            setters = {
                'fontsize': ilabel.set_fontsize,
                'fontweight': ilabel.set_weight,
                'color': ilabel.set_color,
                'alpha': ilabel.set_alpha,
                'rotation': ilabel.set_rotation,
            }
            for key, setter in setters.items():
                if key in tick_kwargs and tick_kwargs[key] is not None:
                    setter(tick_kwargs[key])


    def set_spines(self, **spines_kwargs):
        spines = []
        for iax in [self.ax1, self.ax2, self.ax3, self.cax]:
            if iax is None: continue
            try:
                tmp = iax.axis["left"]
                spines += [iax.axis["left"].line, iax.axis["right"].line, iax.axis["top"].line, iax.axis["bottom"].line]
            except:
                spines += iax.spines.values()
        for spine in spines:

            setters = {
                'width': spine.set_linewidth,
                'color': spine.set_color,
                'linestyle': spine.set_linestyle,
                'alpha': spine.set_alpha,
                # 'visible': spine.set_visible,
            }

            for key, setter in setters.items():
                if key in spines_kwargs and spines_kwargs[key] is not None:
                    setter(spines_kwargs[key])
        if "width" in spines_kwargs:
            ww = spines_kwargs.get("width")
            for iax in [self.ax1, self.ax2, self.ax3, self.cax]:
                if iax is None: continue
                iax.tick_params(axis="both", which="major", width=ww)
        if "color" in spines_kwargs:
            cw = spines_kwargs.get("color")
            for iax in [self.ax1, self.ax2, self.ax3, self.cax]:
                if iax is None: continue
                iax.tick_params(axis="both", which="major", color=cw)

    def set_lines(self, **lines_kwargs):
        for iline in self.vlines + self.hlines:
            if type(iline) is list:
                iline = iline[0]
            setters = {
                'color': iline.set_color,
                'linestyle': iline.set_linestyle,
                'alpha': iline.set_alpha,
                'width': iline.set_linewidths if hasattr(iline, 'set_linewidths') else iline.set_linewidth,
            }
            for key, setter in setters.items():
                if key in lines_kwargs and lines_kwargs[key] is not None:
                    setter(lines_kwargs[key])


    def set_labels(self, **font_kwargs):
        # labels = []
        for ilabel in self.labels:
            setters = {
                'fontsize': ilabel.set_fontsize,
                'fontweight': ilabel.set_fontweight,
                'color': ilabel.set_color,
                'alpha': ilabel.set_alpha,
                'ha': ilabel.set_ha,
                'va': ilabel.set_va,
                'rotation': ilabel.set_rotation,
            }
            for key, setter in setters.items():
                if key in font_kwargs and font_kwargs[key] is not None:
                    setter(font_kwargs[key])

    def set_title(self, text, **font_kwargs):
        '''
        Parameters
        ----------
        text : str
            Title text
        '''
        if self.title_text is None:
            return

        self.title_text.set_text(text)

        setters = {
            'fontsize': self.title_text.set_fontsize,
            'fontweight': self.title_text.set_fontweight,
            'color': self.title_text.set_color,
            'alpha': self.title_text.set_alpha,
            'ha': self.title_text.set_ha,
            'va': self.title_text.set_va,
            'rotation': self.title_text.set_rotation,

        }

        for key, setter in setters.items():
            if key in font_kwargs and font_kwargs[key] is not None:
                setter(font_kwargs[key])


def grey3flat(
    data: Union[np.ndarray, "Rsfarray"],
    *,
    frame1: int = 0, frame2: int = 0, frame3: int = 0,
    point1: float = 0.75, point2: float = 0.5,
    d1: Optional[float] = None, o1: Optional[float] = None,
    d2: Optional[float] = None, o2: Optional[float] = None,
    d3: Optional[float] = None, o3: Optional[float] = None,
    label1: Optional[str] = None, label2: Optional[str] = None, label3: Optional[str] = None,
    title: Optional[str] = None,
    vmin: Optional[float] = None, vmax: Optional[float] = None,
    clip: Optional[float] = None, pclip: Optional[float] = 99,
    bias: Optional[float] = None, allpos: bool = False,
    cmap: str = "seismic",
    colorbar: bool = False,
    ax: Optional[plt.Axes] = None,
    cax: Optional[plt.Axes] = None,
    wig: bool = False,
    **plot_params
) -> plt.Figure:
    """
    grey3
    """
    gattr = Grey3Attributes()

    data = np.squeeze(data)
    if data.ndim < 3:
        raise ValueError(f"Input data must be at least 3D, got shape {data.shape}")
    elif data.ndim > 3:
        warnings.warn("Got data dimensions > 3, use first cube.")
        data = data.reshape(data.shape[0], data.shape[1], data.shape[2], -1)[:, :, :, 0]

    ny, nx, nz = data.shape
    frame1 = min(max(0, int(frame1)), ny - 1)
    frame2 = min(max(0, int(frame2)), nx - 1)
    frame3 = min(max(0, int(frame3)), nz - 1)

    if hasattr(data, "d1"):
        d1 = d1 if d1 is not None else getattr(data, "d1", 1.0)
        d2 = d2 if d2 is not None else getattr(data, "d2", 1.0)
        d3 = d3 if d3 is not None else getattr(data, "d3", 1.0)
        o1 = o1 if o1 is not None else getattr(data, "o1", 0.0)
        o2 = o2 if o2 is not None else getattr(data, "o2", 0.0)
        o3 = o3 if o3 is not None else getattr(data, "o3", 0.0)
        axis1, axis2, axis3 = data.axis([0,1,2])
        if label1 is None and hasattr(data, "label_unit"):
            label1 = data.label_unit(axis=0)
        if label2 is None and hasattr(data, "label_unit"):
            label2 = data.label_unit(axis=1)
        if label3 is None and hasattr(data, "label_unit"):
            label3 = data.label_unit(axis=2)
    else:
        d1 = 1.0 if d1 is None else d1
        d2 = 1.0 if d2 is None else d2
        d3 = 1.0 if d3 is None else d3
        o1 = 0.0 if o1 is None else o1
        o2 = 0.0 if o2 is None else o2
        o3 = 0.0 if o3 is None else o3
        axis1 = np.linspace(o1, o1 + d1 * ny, ny)
        axis2 = np.linspace(o2, o2 + d2 * nx, nx)
        axis3 = np.linspace(o3, o3 + d3 * nz, nz)

    if hasattr(data, "window"):
        slice1 = data.window(n3=1, f3=frame3)
        slice2 = data.window(n2=1, f2=frame2)
        slice3 = data.window(n1=1, f1=frame1)
    else:
        slice1 = data[:, :, frame3]  # n1-n2
        slice2 = data[:, frame2, :]  # n1-n3
        slice3 = data[frame1, :, :]  # n2-n3

    allslice = np.concatenate([slice1.ravel(), slice2.ravel(), slice3.ravel()])
    if bias is None:
        bias = 0
    if pclip is not None and clip is None and vmin is None and vmax is None:
        clip_val = np.percentile(np.abs(allslice), pclip)
        if allpos:
            vmin, vmax = 0, clip_val
        else:
            vmin, vmax = bias - clip_val, bias + clip_val
    if clip is not None:
        clip = abs(clip)
        if allpos:
            vmin, vmax = 0, clip
        else:
            vmin, vmax = bias - clip, bias + clip
    elif vmin is not None or vmax is not None:
        if vmin is None: vmin = -vmax
        if vmax is None: vmax = -vmin
        if vmin > vmax: vmin, vmax = vmax, vmin
    
    if data.dtype == np.uint8:
        vmin, vmax = 0, 255

    if ax is None:
        fig, ax = plt.subplots()
    else:
        fig = ax.figure
    ax_main = ax
    ax_main.axis("off")
    gattr.fig = fig
    gattr.main_ax = ax_main

    if title: 
        height = 0.9
    else:
        height = 1

    if colorbar and cax is None and not wig:
        # Stole colorbar axes
        width = 0.875
    else: width = 1.

    ax1 = ax_main.inset_axes([0.0, 0.0, point2 * width, point1 * height])
    ax2 = ax_main.inset_axes([point2*width, 0.0, (1 - point2) * width, point1 * height], sharey=ax1)
    ax3 = ax_main.inset_axes([0.0, point1 * height, point2 * width, (1 - point1) * height], sharex=ax1)
    gattr.ax1 = ax1
    gattr.ax2 = ax3
    gattr.ax3 = ax2

    if wig:
        wigparas = {
            'zplot': plot_params.get('zplot', 1),
            'ncolor': plot_params.get('ncolor', 'blue'),
            'pcolor': plot_params.get('pcolor', 'red'),
            'lcolor': plot_params.get('lcolor', 'black'),
        }
        wiggle(slice1, ax=ax1, clip=clip, show=False, transp=True, yreverse=True, **wigparas)
        wiggle(slice2, ax=ax2, clip=clip, show=False, transp=True, yreverse=True, **wigparas)
        wiggle(slice3, ax=ax3, clip=clip, show=False, transp=False, yreverse=False, **wigparas)
        colorbar=False
    else:
        grey(slice1, ax=ax1, cmap=cmap, vmin=vmin, vmax=vmax, show=False)
        grey(slice2, ax=ax2, cmap=cmap, vmin=vmin, vmax=vmax, show=False)
        grey(slice3, ax=ax3, cmap=cmap, vmin=vmin, vmax=vmax, show=False, transp=False, yreverse=False)

        im1 = ax1.images[0]
        im2 = ax2.images[0]
        im3 = ax3.images[0]
        gattr.im1 = im1
        gattr.im2 = im2
        gattr.im3 = im3
        gattr.labels.append(ax1.xaxis.label)
        gattr.labels.append(ax2.xaxis.label)
        gattr.labels.append(ax1.yaxis.label)
        gattr.labels.append(ax3.yaxis.label)



    ax2.yaxis.set_visible(False)
    ax3.xaxis.set_visible(False)

    for itklabel in ax1.xaxis.get_ticklabels() +\
        ax1.yaxis.get_ticklabels() + \
        ax2.xaxis.get_ticklabels() + \
        ax2.yaxis.get_ticklabels() + \
        ax3.xaxis.get_ticklabels() + \
        ax3.yaxis.get_ticklabels():
        gattr.ticklabels.append(itklabel)

    lcol = plot_params.get('framelinecol', 'blue' if cmap == 'grey' else 'black')
    gattr.hlines.append(ax1.hlines(y=axis1[frame1], xmin=axis2[0], xmax=axis2[-1], color=lcol))
    gattr.vlines.append(ax1.vlines(x=axis2[frame2], ymin=axis1[0], ymax=axis1[-1], color=lcol))
    gattr.hlines.append(ax2.hlines(y=axis1[frame1], xmin=axis3[0], xmax=axis3[-1], color=lcol))
    gattr.vlines.append(ax2.vlines(x=axis3[frame3], ymin=axis1[0], ymax=axis1[-1], color=lcol))
    gattr.hlines.append(ax3.hlines(y=axis3[frame3], xmin=axis2[0], xmax=axis2[-1], color=lcol))
    gattr.vlines.append(ax3.vlines(x=axis2[frame2], ymin=axis3[0], ymax=axis3[-1], color=lcol))

    tlabelpad = 0.01
    gattr.ticklabels.append(ax2.text(x=1+tlabelpad,  y=1 - frame1/ny, s=f"{axis1[frame1]:.3g}", ha="left", va="center", color=lcol, rotation=90, transform=ax2.transAxes))
    gattr.ticklabels.append(ax2.text(x=frame3/nz,     y=1+tlabelpad, s=f"{axis3[frame3]:.3g}", ha="left", va="bottom", color=lcol, rotation=0, transform=ax2.transAxes))
    gattr.ticklabels.append(ax3.text(x=frame2/nx, y=1+tlabelpad, s=f"{axis2[frame2]:.3g}", ha="center", va="bottom", color=lcol, rotation=0, transform=ax3.transAxes))

    def _set_indicator_frame(gattr=gattr, frame1=frame1, frame2=frame2, frame3=frame3):
        hline1 = gattr.hlines[0].get_segments()
        hline1[0][0, 1] = axis1[frame1]
        hline1[0][1, 1] = axis1[frame1]
        gattr.hlines[0].set_segments(hline1)

        hline2 = gattr.hlines[1].get_segments()
        hline2[0][0, 1] = axis1[frame1]
        hline2[0][1, 1] = axis1[frame1]
        gattr.hlines[1].set_segments(hline2)

        hline3 = gattr.hlines[2].get_segments()
        hline3[0][0, 1] = axis3[frame3]
        hline3[0][1, 1] = axis3[frame3]
        gattr.hlines[2].set_segments(hline3)

        vline1 = gattr.vlines[0].get_segments()
        vline1[0][0, 0] = axis2[frame2]
        vline1[0][1, 0] = axis2[frame2]
        gattr.vlines[0].set_segments(vline1)

        vline2 = gattr.vlines[1].get_segments()
        vline2[0][0, 0] = axis3[frame3]
        vline2[0][1, 0] = axis3[frame3]
        gattr.vlines[1].set_segments(vline2)

        vline3 = gattr.vlines[2].get_segments()
        vline3[0][0, 0] = axis2[frame2]
        vline3[0][1, 0] = axis2[frame2]
        gattr.vlines[2].set_segments(vline3)
        # gattr.ticklabels.append(
        #     ax2.text(x=1 + tlabelpad, y=1 - frame1 / ny, s=f"{axis1[frame1]:.3g}", ha="left", va="center", color=lcol,
        #              rotation=90, transform=ax2.transAxes))
        # gattr.ticklabels.append(
        #     ax2.text(x=frame3 / nz, y=1 + tlabelpad, s=f"{axis3[frame3]:.3g}", ha="left", va="bottom", color=lcol,
        #              rotation=0, transform=ax2.transAxes))
        # gattr.ticklabels.append(
        #     ax3.text(x=frame2 / nx, y=1 + tlabelpad, s=f"{axis2[frame2]:.3g}", ha="center", va="bottom", color=lcol,
        #              rotation=0, transform=ax3.transAxes))

        gattr.ticklabels[-3].set_position([1+tlabelpad, 1 - frame1/ny])
        gattr.ticklabels[-3].set_text(f"{axis1[frame1]:.3g}")
        gattr.ticklabels[-2].set_position([frame3 / nz, 1 + tlabelpad])
        gattr.ticklabels[-2].set_text(f"{axis3[frame3]:.3g}")
        gattr.ticklabels[-1].set_position([frame2 / nx, 1 + tlabelpad])
        gattr.ticklabels[-1].set_text(f"{axis2[frame2]:.3g}")

    setattr(gattr, 'set_indicator_frame', _set_indicator_frame)

    ticks1 = ax1.get_yticks()
    ticks2 = ax3.get_yticks()
    all_ticks = np.concatenate([ticks1, ticks2])
    diffs = np.diff(np.unique(np.round(all_ticks, 10)))
    min_step = np.min(np.abs(diffs[diffs > 0]))
    decimals = max(0, -int(np.floor(np.log10(min_step))))
    fmt_str = f"%.{decimals}f"
    ax3.yaxis.set_major_formatter(mticker.FormatStrFormatter(fmt_str))
    ax1.yaxis.set_major_formatter(mticker.FormatStrFormatter(fmt_str))

    # ---- 8. colorbar ----
    if colorbar:
        if cax is not None:
            gattr.cbar = fig.colorbar(im1, cax=cax)
        else:
            cwidth = (1 - width)/3
            cax = ax_main.inset_axes([width+cwidth, 0.0, cwidth, height])
            gattr.cbar = fig.colorbar(im1, cax=cax)
        gattr.cax = cax

    if title:
        ax_title = ax_main.inset_axes([0.0, height, 1, 1 - height])
        gattr.title_text = ax_title.text(0.5, 0.5, title, ha="center", va="bottom", fontsize=12)
        ax_title.axis("off")
        gattr.title_ax = ax_title

    if plot_params.get('show', True):
        plt.show()

    # return fig
    return gattr


def grey3cube(
    data: Union[np.ndarray, "Rsfarray"],
    *,
    frame1: int = 0, frame2: int = 0, frame3: int = 0,
    point1: float = 0.75, point2: float = 0.5,
    d1: Optional[float] = None, o1: Optional[float] = None,
    d2: Optional[float] = None, o2: Optional[float] = None,
    d3: Optional[float] = None, o3: Optional[float] = None,
    label1: Optional[str] = None, label2: Optional[str] = None, label3: Optional[str] = None,
    title: Optional[str] = None,
    vmin: Optional[float] = None, vmax: Optional[float] = None,
    clip: Optional[float] = None, pclip: Optional[float] = 99,
    bias: Optional[float] = None, allpos: bool = False,
    cmap: str = "seismic",
    colorbar: bool = False,
    ax: Optional[plt.Axes] = None,
    cax: Optional[plt.Axes] = None,
    **plot_params
) -> plt.Figure:
   
    gattr = Grey3Attributes()
    data = np.squeeze(data)
    if data.ndim < 3:
        raise ValueError(f"Input data must be at least 3D, got shape {data.shape}")
    elif data.ndim > 3:
        warnings.warn("Got data dimensions > 3, use first cube.")
        data = data.reshape(data.shape[0], data.shape[1], data.shape[2], -1)[:, :, :, 0]


    ny, nx, nz = data.shape
    frame1 = min(ny-1, frame1)
    frame2 = min(nx-1, frame2)
    frame3 = min(nz-1, frame3)
    if hasattr(data, "d1"):
        d1 = d1 if d1 is not None else getattr(data, "d1", 1.0)
        d2 = d2 if d2 is not None else getattr(data, "d2", 1.0)
        d3 = d3 if d3 is not None else getattr(data, "d3", 1.0)
        o1 = o1 if o1 is not None else getattr(data, "o1", 0.0)
        o2 = o2 if o2 is not None else getattr(data, "o2", 0.0)
        o3 = o3 if o3 is not None else getattr(data, "o3", 0.0)
        axis1, axis2, axis3 = data.axis([0,1,2])
        if label1 is None and hasattr(data, "label_unit"):
            label1 = data.label_unit(axis=0)
        if label2 is None and hasattr(data, "label_unit"):
            label2 = data.label_unit(axis=1)
        if label3 is None and hasattr(data, "label_unit"):
            label3 = data.label_unit(axis=2)
    else:
        d1 = 1.0 if d1 is None else d1
        d2 = 1.0 if d2 is None else d2
        d3 = 1.0 if d3 is None else d3
        o1 = 0.0 if o1 is None else o1
        o2 = 0.0 if o2 is None else o2
        o3 = 0.0 if o3 is None else o3
        axis1 = np.linspace(o1, o1 + d1 * ny, ny)
        axis2 = np.linspace(o2, o2 + d2 * nx, nx)
        axis3 = np.linspace(o3, o3 + d3 * nz, nz)

    if hasattr(data, "window"):
        slice1 = data.window(n3=1, f3=frame3)
        slice2 = data.window(n2=1, f2=frame2)
        slice3 = data.window(n1=1, f1=frame1)
    else:
        slice1 = data[:, :, frame3]  # n1-n2
        slice2 = data[:, frame2, :]  # n1-n3
        slice3 = data[frame1, :, :]  # n2-n3
    allslice = np.concatenate([slice1.ravel(), slice2.ravel(), slice3.ravel()])
    if bias is None:
        bias = 0
    if pclip is not None and clip is None and vmin is None and vmax is None:
        clip_val = np.percentile(np.abs(allslice), pclip)
        if allpos:
            vmin, vmax = 0, clip_val
        else:
            vmin, vmax = bias - clip_val, bias + clip_val
    if clip is not None:
        clip = abs(clip)
        if allpos:
            vmin, vmax = 0, clip
        else:
            vmin, vmax = bias - clip, bias + clip
    elif vmin is not None or vmax is not None:
        if vmin is None: vmin = -vmax
        if vmax is None: vmax = -vmin
        if vmin > vmax: vmin, vmax = vmax, vmin

    if data.dtype == np.uint8:
        vmin, vmax = 0, 255



    n1tic, n2tic, n3tic = plot_params.get("n1tic", 5), plot_params.get("n2tic", 5), plot_params.get("n3tic", 5)
    format1, format2, format3 = plot_params.get("format1", None), plot_params.get("format2", None), plot_params.get("format3", None)
    
    amax1, amax2, amax3 = axis1[-1], axis2[-1], axis3[-1]
    len1, len2, len3 = amax1 - o1, amax2 - o2, amax3 - o3
    if len1==0:
        len1 = 1
        o1 = 0
        amax1 = 1
    if len2==0:
        len2 = 1
        o2 = 0
        amax2 = 1
    if len3==0:
        len3 = 1
        o3 = 0
        amax3 = 1
    extents = [
        [o2, amax2, amax1, o1],
        [0, len2, len3, 0],
        [0, len3, 0, len1],
    ]

    axis_xlims = [
        [o2, amax2],
        [o2, amax2 + len2 / point2 * (1 - point2)],
        [o3, amax3]
    ]
    axis_ylims = [
        [amax1, o1],
        [o3, amax3],
        [o1, amax1 + len1 / point1 * (1 - point1)]
    ]


    axshear = (1 - point2) / len3 * len2 / point2
    axshift = -(1 - point2) / len3 * len2 / point2 * o3
    dxshear = (1 - point2) / len3
    dxshift = 0
    ayshear = (1 - point1) / len3 * len1 / point1
    ayshift = -(1 - point1) / len3 * len1 / point1 * o3
    dyshear = (1 - point1) / len3
    dyshift = 0



    dtrans1 = transforms.Affine2D(
        np.array([[point2 / len2, dxshear, 0],
         [0, 1 / len3, 0],
         [0, 0, 1]])
    )
    dtrans2 = transforms.Affine2D(
        np.array([[1 / len3, 0, 0],
         [dyshear, point1 / len1, 0],
         [0, 0, 1]])
    )
    atrans1 = transforms.Affine2D(
        np.array([[1, axshear, axshift],
         [0, 1, 0],
         [0, 0, 1]])
    )
    atrans2 = transforms.Affine2D(
        np.array([[1, 0, 0],
         [ayshear, 1, ayshift],
         [0, 0, 1]])
    )

    grid_helper = artist.floating_axes.GridHelperCurveLinear(atrans1,
                                        extremes=[o2, amax2, o3, amax3],
                                        grid_locator1=artist.grid_finder.MaxNLocator(nbins=n2tic),
                                        grid_locator2=artist.grid_finder.MaxNLocator(nbins=n3tic),
                                         tick_formatter2 = mticker.FormatStrFormatter(format3) if format3 is not None else None,
                                                             )
    grid_helper1 = artist.floating_axes.GridHelperCurveLinear(atrans2,
                                         extremes=[o3, amax3, o1, amax1],
                                         grid_locator1=artist.grid_finder.MaxNLocator(nbins=n1tic),
                                         grid_locator2=artist.grid_finder.MaxNLocator(nbins=n1tic),
                                         )

    topmargin = 0.05


    if ax is None:
        fig, ax = plt.subplots()
    else:
        fig = ax.figure
    axbase0 = ax
    gattr.fig = fig
    gattr.main_ax = axbase0

    axbase0.axis('off')
    axbase0.set_label("Base axes")

    if title:
        title_pos = [0, 1 - topmargin, 1, topmargin]
        ax_title = axbase0.inset_axes(title_pos)
        gattr.title_ax = ax_title
        ax_title.set_frame_on(False)
        ax_title.get_xaxis().set_visible(False)
        ax_title.get_yaxis().set_visible(False)
        ax_title.set_xlim([0, 1])
        ax_title.set_ylim([0, 1])
        gattr.title_text = ax_title.text(0.5, 1, title, va='bottom', ha='center')
        axbase0.add_child_axes(ax_title)
    else:
        topmargin = 0.
    hei_ax = 1 - topmargin
    

    if colorbar and cax is None:
        axbase = axbase0.inset_axes(bounds=[0, 0, 0.85, 1 - topmargin], facecolor="none")
        gattr.main_ax = axbase
        axbar = axbase0.inset_axes(bounds=[0.90, 0, 0.05, 1 - topmargin], facecolor="none")
        gattr.cax = axbar
        axbase.set_label("Base axes with bar")
    else:
        axbase = axbase0
    axbase.axis('off')

    ax1 = axbase.inset_axes(bounds=[0, 0, point2, point1*hei_ax], facecolor="none")
    ax1.set_label("Axes 1 [n1, n2]")
    gattr.ax1 = ax1

    im0 = ax1.imshow(slice1, aspect="auto", extent=extents[0],
                     cmap=cmap, vmin=vmin, vmax=vmax)
    gattr.im1 = im0

    ax1.yaxis.set_major_locator(MaxNLocator1(nbins=n1tic))
    ax1.set_xlim(axis_xlims[0])
    ax1.set_ylim(axis_ylims[0])

    ax2 = axbase.inset_axes(bounds=[0, point1*hei_ax, 1, (1 - point1)*hei_ax],
                            axes_class=artist.Axes, grid_helper=grid_helper,
                            facecolor="none")
    ax2.set_label("Axes 3 [n1, n3]")
    gattr.ax2 = ax2

    im1 = ax2.imshow(slice3.T, aspect="auto", extent=extents[1],
                     cmap=cmap, vmin=vmin, vmax=vmax)
    gattr.im2 = im1
 
    im1.set_transform(dtrans1 + ax2.transAxes)

    #
    ax2.set_ylim(axis_ylims[1])
    ax2.set_xlim(axis_xlims[1])
    # ax2.axis["t1"] = ax2.new_floating_axis(0,-5)

    ax3 = axbase.inset_axes(bounds=[point2, 0, 1 - point2, hei_ax],
                            axes_class=artist.Axes, grid_helper=grid_helper1,
                            facecolor="none")
    ax3.set_label("Axes 2 [n2, n3]")
    gattr.ax3 = ax3

    im2 = ax3.imshow(slice2, aspect="auto", extent=extents[2],
                     cmap=cmap, vmin=vmin, vmax=vmax)
    im2.set_transform(dtrans2 + ax3.transAxes)
    gattr.im3 = im2
    #
    ax3.set_xlim(axis_xlims[2])
    ax3.set_ylim(axis_ylims[2])

    ax2.axis[:].major_ticklabels.set(visible=False)
    ax2.axis[:].major_ticks.set(visible=False)

  

    
    ax2.axis["left"].major_ticks.set(tick_out=True, visible=True)
    ax2.axis["right"].set(visible=False)
    ax2.axis["left"].major_ticklabels.set(visible=True)



    ax3.axis[:].major_ticklabels.set(visible=False)
    ax3.axis[:].major_ticks.set(visible=False)
    ax3.axis["right"].major_ticks.set(visible=False)

    if format1 is not None: ax1.xaxis.set_major_formatter(mticker.FormatStrFormatter(format1))
    if format2 is not None: ax1.yaxis.set_major_formatter(mticker.FormatStrFormatter(format2))

    # Labels
    ax1.set_xlabel(label2)
    ax1.set_ylabel(label1)

    ax2.axis["left"].label.set(text=label3)

    gattr.labels.append(ax1.xaxis.label)
    gattr.labels.append(ax1.yaxis.label)
    gattr.labels.append(ax2.axis["left"].label)

    for itklabel in ax1.xaxis.get_ticklabels() +\
        ax1.yaxis.get_ticklabels():
        gattr.ticklabels.append(itklabel)
    gattr.ticklabels.append(ax2.axis["left"].major_ticklabels)

    # Z-order
    ax1.spines[:].set_zorder(10)
    ax2.axis[:].line.set_zorder(10)
    ax3.axis[:].line.set_zorder(10)


    # Colorbar
    if colorbar and cax is None:
        # cbar = fig.colorbar(im0, cax=axbar, label=bartitle, orientation=barpos)
        cbar = fig.colorbar(im0, cax=axbar, cmap=cmap, label=plot_params.get('bartitle', ''),)
        gattr.cbar = cbar

    # Indicating lines
    l11 = (amax1 - axis1[frame1]) / len1 * point1
    l12 = (axis2[frame2] - o2) / len2 * point2

    loff1 = (axis3[frame3] - o3) / len3 * (1 - point1)
    loff2 = (axis3[frame3] - o3) / len3 * (1 - point2)

    l21 = point1 + loff1
    l22 = point2 + loff2
    l221 = l12 +  (1-point2)

    l31 = l11 + 1 - point1
    l32 = point1 + loff1

    lcol = plot_params.get('framelinecol', 'blue' if cmap == 'grey' else 'black')

    gattr.hlines.append(ax1.hlines(l11*hei_ax,0,point2, color=lcol,transform=axbase.transAxes))
    gattr.vlines.append(ax1.vlines(l12,0,point1*hei_ax, color=lcol, transform=axbase.transAxes))
    gattr.hlines.append(ax2.plot([loff2,l22],[l21*hei_ax,l21*hei_ax], color=lcol, transform=axbase.transAxes))
    gattr.vlines.append(ax2.plot([l12,l221],[point1*hei_ax,1*hei_ax], color=lcol, transform=axbase.transAxes))
    gattr.vlines.append(ax3.vlines(l22, loff1*hei_ax, l32*hei_ax, color=lcol,  transform=axbase.transAxes))
    gattr.hlines.append(ax3.plot([point2,1],[l11*hei_ax,l31*hei_ax], color=lcol, transform=axbase.transAxes))


    # Indicating labels
    lab1 = str(axis2[frame2])
    lab11 = "%.2f" % axis2[frame2]
    lab1 = lab11 if len(lab11) < len(lab1) else lab1
    lab2 = str(axis1[frame1])
    lab21 = "%.2f" % axis1[frame1]
    lab2 = lab21 if len(lab21) < len(lab2) else lab2
    lab3 = str(axis3[frame3])
    lab31 = "%.2f" % axis3[frame3]
    lab3 = lab31 if len(lab31) < len(lab3) else lab3
    gattr.ticklabels.append(axbase.text(l221, 1*hei_ax, "%s" % lab1, va='bottom',
                ha='center', color=lcol))
    gattr.ticklabels.append(axbase.text(1, l31*hei_ax, "%s" % lab2, va='center',
                ha='left', color=lcol, rotation=-90))
    gattr.ticklabels.append(axbase.text(l22, loff1*hei_ax,
                "%s" % lab3, va='top', ha='left', color=lcol))

    def _set_indicator_frame(gattr=gattr, frame1=frame1, frame2=frame2, frame3=frame3):
        l11 = (amax1 - axis1[frame1]) / len1 * point1
        l12 = (axis2[frame2] - o2) / len2 * point2

        loff1 = (axis3[frame3] - o3) / len3 * (1 - point1)
        loff2 = (axis3[frame3] - o3) / len3 * (1 - point2)

        l21 = point1 + loff1
        l22 = point2 + loff2
        l221 = l12 + (1 - point2)

        l31 = l11 + 1 - point1
        l32 = point1 + loff1

        hline1 = gattr.hlines[0].get_segments()
        hline1[0][0,1] = l11*hei_ax
        hline1[0][1,1] = l11*hei_ax
        gattr.hlines[0].set_segments(hline1)

        hline2, = gattr.hlines[1]
        hline2.set_xdata([loff2,l22])
        hline2.set_ydata([l21*hei_ax,l21*hei_ax])

        hline3, = gattr.hlines[2]
        hline3.set_xdata([point2,1])
        hline3.set_ydata([l11*hei_ax,l31*hei_ax])

        vline1 = gattr.vlines[0].get_segments()
        vline1[0][0,0] = l12
        vline1[0][1,0] = l12
        gattr.vlines[0].set_segments(vline1)

        vline2, = gattr.vlines[1]
        vline2.set_xdata([l12,l221])
        vline2.set_ydata([point1*hei_ax,1*hei_ax])

        vline3 = gattr.vlines[2].get_segments()
        vline3[0][0,0] = l22
        vline3[0][1,0] = l22
        vline3[0][0,1] = loff1 * hei_ax
        vline3[0][1,1] = l32 * hei_ax
        gattr.vlines[2].set_segments(vline3)

        # Indicating labels
        lab1 = str(axis2[frame2])
        lab11 = "%.2f" % axis2[frame2]
        lab1 = lab11 if len(lab11) < len(lab1) else lab1
        lab2 = str(axis1[frame1])
        lab21 = "%.2f" % axis1[frame1]
        lab2 = lab21 if len(lab21) < len(lab2) else lab2
        lab3 = str(axis3[frame3])
        lab31 = "%.2f" % axis3[frame3]
        lab3 = lab31 if len(lab31) < len(lab3) else lab3
        gattr.ticklabels[-3].set_position([l221, 1 * hei_ax])
        gattr.ticklabels[-3].set_text("%s" % lab1)
        gattr.ticklabels[-2].set_position([1, l31 * hei_ax])
        gattr.ticklabels[-2].set_text("%s" % lab2)
        gattr.ticklabels[-1].set_position([l22, loff1 * hei_ax])
        gattr.ticklabels[-1].set_text("%s" % lab3)


    setattr(gattr, 'set_indicator_frame', _set_indicator_frame)
    return gattr


def grey3(*args, **kargs):
    if kargs.get('flat', False):
        return grey3flat(*args, **kargs)
    else:
        return grey3cube(*args, **kargs)