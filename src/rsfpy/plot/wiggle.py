import numpy as np
import matplotlib.pyplot as plt
from typing import Optional, Union
from matplotlib.collections import LineCollection, PolyCollection
import warnings

def wiggle(
    data: Union[np.ndarray, "Rsfarray"],
    *,
    d1: Optional[float] = None, o1: Optional[float] = None,
    d2: Optional[float] = None, o2: Optional[float] = None,
    min1: Optional[float] = None, max1: Optional[float] = None,
    min2: Optional[float] = None, max2: Optional[float] = None,
    label1: Optional[str] = None, label2: Optional[str] = None,
    title: Optional[str] = None,
    ax: Optional[plt.Axes] = None,
    ncolor: str = 'blue',
    pcolor: str = 'red',
    lcolor: str = 'black',
    zplot: float = 1.0,
    transp: Optional[bool] = False,
    yreverse: Optional[bool] = False,
    xreverse: Optional[bool] = False,
    **plot_params: Optional[dict]
) -> plt.Axes:
    """
    Plot 2D wiggle image.
    """
    data = np.squeeze(data)
    if data.ndim < 2:
        raise ValueError("Input data must be at least 2D.")
    elif data.ndim > 2:
        warnings.warn("Got data dimensions > 2, use first slice.")
        data = data.reshape(data.shape[0], data.shape[1], -1)[:, :, 0]

    defaults = {
        'clip': None,
        'bias': 0,
        'allpos': False,
        'pclip': 99,
        'linewidth': 0.5,
        'interpolate': False,   # 保留参数，但这里不再使用 fill_between 的 interpolate
    }
    params = {**defaults, **plot_params}

    if ax is None:
        fig, ax = plt.subplots()
    else:
        fig = ax.figure

    if hasattr(data, "d1"):
        d1 = d1 if d1 is not None else getattr(data, "d1", None)
        d2 = d2 if d2 is not None else getattr(data, "d2", None)
        o1 = o1 if o1 is not None else getattr(data, "o1", None)
        o2 = o2 if o2 is not None else getattr(data, "o2", None)
        if label1 is None and hasattr(data, "label_unit"):
            label1 = data.label_unit(axis=0)
        if label2 is None and hasattr(data, "label_unit"):
            label2 = data.label_unit(axis=1)

    if d1 is None or d1 == 0:
        d1 = 1.
    if d2 is None or d2 == 0:
        d2 = 1.
    if o1 is None:
        o1 = 0.
    if o2 is None:
        o2 = 0.

    n1, n2 = data.shape

    if hasattr(data, "axis"):
        axis1, axis2 = data.axis([0, 1])
    else:
        axis1 = np.arange(n1) * d1 + o1
        axis2 = np.arange(n2) * d2 + o2

    if min1 is None:
        min1 = o1
    if max1 is None:
        max1 = o1 + d1 * (n1 - 1)
    if min2 is None:
        min2 = o2
    if max2 is None:
        max2 = o2 + d2 * (n2 - 1)

    t = axis1
    x_positions = axis2

    clip = params['clip']
    bias = params['bias']
    pclip = params['pclip']
    allpos = params['allpos']

    if clip is None:
        if pclip is not None:
            if hasattr(data, "pclip"):
                clip = data.pclip(pclip)
            else:
                clip = np.percentile(np.abs(data), pclip)
        else:
            clip = np.max(np.abs(data))

    clip = abs(clip)
    if clip == 0:
        clip = 1.0

    if allpos:
        vmin, vmax = 0, clip
    else:
        vmin, vmax = bias - clip, bias + clip

    if zplot == 0:
        zplot = 1.0

    default_amp = (o2 + d2 * (n2 - 1) - o2) / n2 / 2
    scale = default_amp * abs(zplot) / clip

    mask = (x_positions >= min2) & (x_positions <= max2)
    sel_xpos = x_positions[mask]
    sel_data = data[:, mask] - bias
    nsel = sel_data.shape[1]

    if nsel == 0:
        return ax

    wiggles = sel_xpos[None, :] + sel_data * scale

    # 画线：一次性 LineCollection
    if not transp:
        segs = [np.column_stack((t, wiggles[:, i])) for i in range(nsel)]
    else:
        segs = [np.column_stack((wiggles[:, i], t)) for i in range(nsel)]
    lc = LineCollection(segs, colors=lcolor, linewidths=params['linewidth'])
    ax.add_collection(lc)

    # 正值填充：用 polygon 代替 fill_between
    if pcolor != 'none':
        polys = []
        for i in range(nsel):
            trace = sel_data[:, i]
            pos = trace > 0
            if not np.any(pos):
                continue

            # 找连续正区间
            idx = np.flatnonzero(pos)
            splits = np.where(np.diff(idx) > 1)[0] + 1
            groups = np.split(idx, splits)

            x0 = sel_xpos[i]
            wx = wiggles[:, i]

            for g in groups:
                if g.size == 0:
                    continue

                tt = t[g]
                ww = wx[g]

                if not transp:
                    poly = np.column_stack((
                        np.r_[tt, tt[::-1]],
                        np.r_[np.full_like(tt, x0), ww[::-1]]
                    ))
                else:
                    poly = np.column_stack((
                        np.r_[np.full_like(tt, x0), ww[::-1]],
                        np.r_[tt, tt[::-1]]
                    ))
                polys.append(poly)

        if polys:
            pc = PolyCollection(polys, facecolors=pcolor, edgecolors='none')
            ax.add_collection(pc)

    # 负值填充：用 polygon 代替 fill_between
    if ncolor != 'none':
        polys = []
        for i in range(nsel):
            trace = sel_data[:, i]
            neg = trace < 0
            if not np.any(neg):
                continue

            idx = np.flatnonzero(neg)
            splits = np.where(np.diff(idx) > 1)[0] + 1
            groups = np.split(idx, splits)

            x0 = sel_xpos[i]
            wx = wiggles[:, i]

            for g in groups:
                if g.size == 0:
                    continue

                tt = t[g]
                ww = wx[g]

                if not transp:
                    poly = np.column_stack((
                        np.r_[tt, tt[::-1]],
                        np.r_[ww, np.full_like(tt, x0)[::-1]]
                    ))
                else:
                    poly = np.column_stack((
                        np.r_[ww, np.full_like(tt, x0)[::-1]],
                        np.r_[tt, tt[::-1]]
                    ))
                polys.append(poly)

        if polys:
            pc = PolyCollection(polys, facecolors=ncolor, edgecolors='none')
            ax.add_collection(pc)

    if not transp:
        min1, max1, min2, max2 = min2, max2, min1, max1

    if label1:
        if transp:
            ax.set_ylabel(label1)
        else:
            ax.set_xlabel(label1)
    if label2:
        if transp:
            ax.set_xlabel(label2)
        else:
            ax.set_ylabel(label2)
    if title:
        ax.set_title(title)

    if yreverse:
        ax.set_ylim(max1, min1)
    else:
        ax.set_ylim(min1, max1)

    if xreverse:
        ax.set_xlim(max2, min2)
    else:
        ax.set_xlim(min2, max2)

    if plot_params.get('show', True):
        plt.show()

    return ax