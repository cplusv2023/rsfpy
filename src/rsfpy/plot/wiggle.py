import numpy as np
import matplotlib.pyplot as plt
from typing import Optional, Union
from matplotlib.collections import LineCollection, PolyCollection
import ctypes, os, warnings

here = os.path.dirname(__file__)
_lib = ctypes.CDLL(f"{here}/librsfpy_utils.so")

_lib.interp_cross.argtypes = [
    ctypes.POINTER(ctypes.c_float), 
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_int,
    ctypes.c_int,    
    ctypes.POINTER(ctypes.c_float), 
    ctypes.POINTER(ctypes.c_float)
]
_lib.interp_cross.restype = ctypes.c_int

def interp_cross(X: np.ndarray, Y: np.ndarray):
    
    if X.dtype != np.float32:
        X = X.astype(np.float32)
    if Y.dtype != np.float32:
        Y = Y.astype(np.float32)

    n, m = Y.shape

    newX = np.zeros(2*n*m, dtype=np.float32)
    newY = np.zeros(2*n*m, dtype=np.float32)

    YY = np.ascontiguousarray(Y.T, dtype=np.float32) 

    count = _lib.interp_cross(
        X.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
        YY.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
        n,
        m,
        newX.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
        newY.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    )

    newX = newX.reshape((m, 2*n)).T
    newY = newY.reshape((m, 2*n)).T
    return newX[:count], newY[:count, :]




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

    Parameters
    ----------
    data : Union[np.ndarray, "Rsfarray"]
        Input data array to be plotted.
    d1, o1, d2, o2 : Optional[float]
        Sampling intervals and origins for the axes.
    min1, max1, min2, max2 : Optional[float]
        Clipping limits for the data.
    label1, label2 : Optional[str]
        Labels for the axes.
    title : Optional[str]
        Title of the plot.
    ax : Optional[plt.Axes]
        Matplotlib axes to plot on.
    ncolor, pcolor, lcolor : str
        Colors for the negative, positive, and line plots.
    zplot : float
        Vertical scaling factor.
    transp : Optional[bool]
        Whether to apply transparency to the plot.
    yreverse, xreverse : Optional[bool]
        Whether to reverse the y or x axis.
    **plot_params : Optional[dict]
        Additional parameters for the plot.

    Returns
    -------
    plt.Axes
        The axes object with the wiggle plot.
    """
    # Check dimensions
    data = np.squeeze(data)
    one_trace = False

    if data.ndim < 2:
        # raise ValueError("Input data must be at least 2D.")
        one_trace = True
        data = data[:, np.newaxis]
    elif data.ndim > 2:
        warnings.warn("Got data dimensions > 2, use first slice.")
        data = data.reshape(data.shape[0], data.shape[1], -1)[:, :, 0]

    defaults = {
        'clip': None,
        'bias': 0,
        'allpos': False,
        'pclip': 99,
        'linewidth': 0.5,
        'interpolate': False
    }
    params = {**defaults, **plot_params}

    # Axes 创建
    if ax is None:
        fig, ax = plt.subplots()
    else:
        fig = ax.figure

    # data = data if transp else data.T
    if hasattr(data, "d1"):
        d1 = d1 if d1 is not None else getattr(data, "d1", None)
        d2 = d2 if d2 is not None else getattr(data, "d2", None)
        o1 = o1 if o1 is not None else getattr(data, "o1", None)
        o2 = o2 if o2 is not None else getattr(data, "o2", None)
        if label1 is None and hasattr(data, "label_unit"):
            label1 = data.label_unit(axis=0)
        if label2 is None and hasattr(data, "label_unit"):
            label2 = data.label_unit(axis=1)
    if d1 is None or d1 == 0: d1 = 1.
    if d2 is None or d2 == 0: d2 = 1.
    if o1 is None: o1 = 0.
    if o2 is None: o2 = 0.

    n1, n2 = data.shape

    if hasattr(data, "axis"):
        axis1, axis2 = data.axis([0, 1])
    else:
        axis1 = np.arange(n1) * d1 + o1
        axis2 = np.arange(n2) * d2 + o2



    if min1 is None: min1 = o1
    if max1 is None: max1 = o1 + d1 * (n1-1)
    if min2 is None: min2 = o2
    if max2 is None: max2 = o2 + d2 * (n2-1)
    
    t = axis1 
    x_positions = axis2

    clip = params['clip']
    bias = params['bias']
    pclip = params['pclip']


    if clip is None:
        if pclip is not None:
            if hasattr(data, "pclip"):
                clip = data.pclip(pclip)
            else:
                clip = np.percentile(np.abs(data), pclip)
        else:
            clip = np.max(np.abs(data))

    clip = abs(clip)

    if zplot == 0:
        zplot = 1.0
    
    if not one_trace:
        default_amp = abs((d2 * (n2-1)) /( 2 * (n2-1)))
    else:
        default_amp = d2 / 2
    scale = default_amp * abs(zplot) / clip

    mask = (x_positions >= min2) & (x_positions <= max2)
    sel_data = data[:, mask] - bias
    sel_xpos = x_positions[mask]
    nsel = sel_data.shape[1]

    # wiggle trace offset
    wiggles = sel_xpos[None, :] + sel_data * scale
    if pcolor != 'none':
        newtp, pwiggles = interp_cross(t, sel_data)
        pwiggles = sel_xpos[None, :] + pwiggles * scale
    if ncolor != 'none':
        newtn, nwiggles = interp_cross(t, sel_data * -1)
        nwiggles = sel_xpos[None, :] + nwiggles * scale * -1
    

    if not transp:
        segs = [np.column_stack([t, wiggles[:, i]]) for i in range(nsel)]
    else:
        segs = [np.column_stack([wiggles[:, i], t]) for i in range(nsel)]

    lc = LineCollection(segs, colors=lcolor, linewidths=params['linewidth'])
    ax.add_collection(lc)

    # fill positive
    if pcolor != 'none':
        polys = []
        for i in range(nsel):
            if np.any(sel_data[:, i] > 0):
                if not transp:
                    polys.append(np.column_stack([newtp[:, i], pwiggles[:, i]]))
                else:
                    polys.append(np.column_stack([pwiggles[:, i], newtp[:, i]]))
        if polys:
            pc = PolyCollection(polys, facecolors=pcolor, edgecolors='none')
            ax.add_collection(pc)

    # fill negative
    if ncolor != 'none':
        polys = []
        for i in range(nsel):
            if np.any(sel_data[:, i] < 0):
                if not transp:
                    polys.append(np.column_stack([newtn[:, i], nwiggles[:, i]]))
                else:
                    polys.append(np.column_stack([nwiggles[:, i], newtn[:, i]]))
        if polys:
            pc = PolyCollection(polys, facecolors=ncolor, edgecolors='none')
            ax.add_collection(pc)

    
    min2 -= d2
    max2 += d2

    if not transp:
        min1, max1, min2, max2 = min2, max2, min1, max1

    if label1:
        if transp:ax.set_ylabel(label1)
        else: ax.set_xlabel(label1)
    if label2:
        if transp:ax.set_xlabel(label2)
        else: ax.set_ylabel(label2)
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
