import numpy as np
import matplotlib.pyplot as plt
import warnings
from typing import Optional, Union

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
    **plot_params
) -> plt.Figure:
    """
    三视图灰度/彩色绘制，框架与 grey 一致
    """
    # ---- 1. 数据检查 ----
    if data.ndim < 3:
        raise ValueError(f"Input data must be at least 3D, got shape {data.shape}")
    elif data.ndim > 3:
        warnings.warn("Got data dimensions > 3, use first cube.")
        data = data.reshape(data.shape[0], data.shape[1], data.shape[2], -1)[:, :, :, 0]

    ny, nx, nz = data.shape
    frame1 = min(max(0, int(frame1)), ny - 1)
    frame2 = min(max(0, int(frame2)), nx - 1)
    frame3 = min(max(0, int(frame3)), nz - 1)

    # ---- 2. 从 Rsfarray 读取属性 ----
    if hasattr(data, "d1"):
        d1 = d1 if d1 is not None else getattr(data, "d1", 1.0)
        d2 = d2 if d2 is not None else getattr(data, "d2", 1.0)
        d3 = d3 if d3 is not None else getattr(data, "d3", 1.0)
        o1 = o1 if o1 is not None else getattr(data, "o1", 0.0)
        o2 = o2 if o2 is not None else getattr(data, "o2", 0.0)
        o3 = o3 if o3 is not None else getattr(data, "o3", 0.0)
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

    # ---- 3. 数据切片 ----
    slice1 = data[:, :, frame3]  # n1-n2
    slice2 = data[:, frame2, :]  # n1-n3
    slice3 = data[frame1, :, :]  # n2-n3

    # ---- 4. 颜色范围计算（与 grey 一致）----
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

    # ---- 5. 创建 Figure 和布局 ----
    if ax is None:
        fig, ax = plt.subplots()
    else:
        fig = ax.figure
    ax_main = ax
    ax_main.axis("off")

    # 三个 inset_axes
    ax1 = ax_main.inset_axes([0.0, 0.0, point2, point1])
    ax2 = ax_main.inset_axes([point2, 0.0, 1 - point2, point1], sharey=ax1)
    ax3 = ax_main.inset_axes([0.0, point1, point2, 1 - point1], sharex=ax1)

    # ---- 6. extent 计算 ----
    extent1 = (o2, o2 + d2 * nx, o1 + d1 * ny, o1)  # slice1
    extent2 = (o3, o3 + d3 * nz, o1 + d1 * ny, o1)  # slice2
    extent3 = (o2, o2 + d2 * nx, o3, o3 + d3 * nz)  # slice3

    # ---- 7. 绘制 ----
    im1 = ax1.imshow(slice1, cmap=cmap, vmin=vmin, vmax=vmax, extent=extent1, aspect="auto")
    ax1.set_xlabel(label2 or "Axis 2")
    ax1.set_ylabel(label1 or "Axis 1")

    im2 = ax2.imshow(slice2, cmap=cmap, vmin=vmin, vmax=vmax, extent=extent2, aspect="auto")
    ax2.set_xlabel(label3 or "Axis 3")
    ax2.yaxis.set_visible(False)

    im3 = ax3.imshow(slice3, cmap=cmap, vmin=vmin, vmax=vmax, extent=extent3, aspect="auto", origin="lower")
    ax3.set_ylabel(label3 or "Axis 3")
    ax3.xaxis.set_visible(False)

    ax_01 = ax0.inset_axes([left_margin, btm_margin, w_ax1, h_ax1])
    ax_01.set_frame_on(False)
    ax_01.xaxis.set_visible(False)
    ax_01.yaxis.set_visible(False)
    ax_01.set_xlim([0, 1])
    ax_01.set_ylim([0, 1])



    # ---- 8. colorbar ----
    if colorbar:
        if cax is not None:
            fig.colorbar(im1, cax=cax)
        else:
            fig.colorbar(im1, ax=[ax1, ax2, ax3])

    if title:
        ax.set_title(title)

    if plot_params.get('show', True):
        plt.show()

    return fig
