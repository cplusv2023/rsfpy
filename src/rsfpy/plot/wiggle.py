# WIGGLE_XPOS_FLAT_VERSION = "2026-05-23-xpos-flatten-n2"
# This version supports xpos by flattening xpos and using the first n2 values only.
import warnings
from typing import Optional, Union, List, Tuple

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection, PolyCollection

try:
    from . import rsfpy_utils as _rsfpy_utils
except Exception:  # pragma: no cover - optional C extension
    _rsfpy_utils = None


ArrayLike = Union[np.ndarray, "Rsfarray"]


def _as_plain_array(a):
    """Return ndarray view/copy while preserving caller-side metadata before use."""
    return np.asarray(a)


def _default_trace_amp(trace_pos: np.ndarray) -> float:
    """Reasonable default wiggle amplitude from possibly irregular trace positions."""
    trace_pos = np.asarray(trace_pos, dtype=float).ravel()
    trace_pos = trace_pos[np.isfinite(trace_pos)]
    if trace_pos.size >= 2:
        vals = np.unique(np.sort(trace_pos))
        diffs = np.diff(vals)
        diffs = diffs[np.isfinite(diffs) & (diffs != 0.0)]
        if diffs.size > 0:
            spacing = np.median(np.abs(diffs))
            if spacing > 0:
                return 0.5 * float(spacing)
    return 0.5


def _make_line_segments(
    t: np.ndarray,
    wiggles: np.ndarray,
    transp: bool,
) -> List[np.ndarray]:
    nsel = wiggles.shape[1]
    if not transp:
        return [np.column_stack((t, wiggles[:, i])) for i in range(nsel)]
    return [np.column_stack((wiggles[:, i], t)) for i in range(nsel)]


def _polys_from_cross_arrays(
    cross_x: np.ndarray,
    cross_y: np.ndarray,
    counts: np.ndarray,
    trace_pos: np.ndarray,
    transp: bool,
) -> List[np.ndarray]:
    """
    Convert zero-crossing half-wave curves to Matplotlib polygons.

    cross_x[:counts[i], i] is axis-1 coordinate, typically time/depth.
    cross_y[:counts[i], i] is scaled signed amplitude relative to the trace
    baseline. trace_pos[i] is the constant spatial baseline for trace i.
    """
    polys: List[np.ndarray] = []
    trace_pos = np.asarray(trace_pos, dtype=float).ravel()
    ntrace = len(counts)

    for i in range(ntrace):
        n = int(counts[i])
        if n < 2:
            continue

        base = float(trace_pos[i])
        if not np.isfinite(base):
            continue

        tt = np.asarray(cross_x[:n, i], dtype=float)
        aa = np.asarray(cross_y[:n, i], dtype=float)

        finite = np.isfinite(tt) & np.isfinite(aa)
        if finite.sum() < 2:
            continue

        tt = tt[finite]
        aa = aa[finite]
        curve = base + aa
        base_arr = np.full_like(tt, base, dtype=float)

        if not transp:
            poly = np.column_stack((
                np.r_[tt, tt[::-1]],
                np.r_[base_arr, curve[::-1]],
            ))
        else:
            poly = np.column_stack((
                np.r_[base_arr, curve[::-1]],
                np.r_[tt, tt[::-1]],
            ))

        polys.append(poly)

    return polys


def _halfwave_cross_py(
    x: np.ndarray,
    y: np.ndarray,
    polarity: int,
):
    """
    Pure Python fallback matching rsfpy_utils.interp_cross.

    Parameters
    ----------
    x : (n1,)
    y : (n1, ntrace), scaled signed amplitude, Fortran-like convention.
    polarity : +1 for y>0, -1 for y<0.
    """
    x = np.asarray(x, dtype=np.float32)
    y = np.asarray(y, dtype=np.float32, order="F")
    n1, ntrace = y.shape
    nmax = 2 * n1

    newx = np.full((nmax, ntrace), np.nan, dtype=np.float32, order="F")
    newy = np.full((nmax, ntrace), np.nan, dtype=np.float32, order="F")
    counts = np.zeros(ntrace, dtype=np.intp)
    maxlen = 0

    def selected(v):
        return v > 0.0 if polarity >= 0 else v < 0.0

    for i2 in range(ntrace):
        yy = y[:, i2]
        count = 0

        if n1 == 1:
            if selected(yy[0]):
                newx[count, i2] = x[0]
                newy[count, i2] = yy[0]
                count += 1
                newx[count, i2] = x[0]
                newy[count, i2] = 0.0
                count += 1
            counts[i2] = count
            maxlen = max(maxlen, count)
            continue

        if selected(yy[0]):
            newx[count, i2] = x[0]
            newy[count, i2] = 0.0
            count += 1

        for i1 in range(n1 - 1):
            y0 = float(yy[i1])
            y1 = float(yy[i1 + 1])
            x0 = float(x[i1])
            x1 = float(x[i1 + 1])
            s0 = selected(y0)
            s1 = selected(y1)

            if s0:
                newx[count, i2] = x0
                newy[count, i2] = y0
                count += 1

            if s0 != s1:
                denom = y0 - y1
                if denom != 0.0:
                    alpha = y0 / denom
                    xcross = x0 + alpha * (x1 - x0)
                else:
                    xcross = x0
                newx[count, i2] = xcross
                newy[count, i2] = 0.0
                count += 1

        if selected(yy[-1]):
            newx[count, i2] = x[-1]
            newy[count, i2] = yy[-1]
            count += 1
            newx[count, i2] = x[-1]
            newy[count, i2] = 0.0
            count += 1

        counts[i2] = count
        maxlen = max(maxlen, count)

    return newx, newy, counts, maxlen


def _halfwave_polys(
    t: np.ndarray,
    amp: np.ndarray,
    trace_pos: np.ndarray,
    polarity: int,
    transp: bool,
    use_c: bool = True,
) -> List[np.ndarray]:
    """Build filled half-wave polygons with zero-crossing interpolation."""
    if amp.size == 0:
        return []

    t32 = np.asarray(t, dtype=np.float32)
    amp32 = np.asarray(amp, dtype=np.float32, order="F")

    if use_c and _rsfpy_utils is not None:
        try:
            cross_x, cross_y, counts, _maxlen = _rsfpy_utils.interp_cross(
                t32, amp32, int(polarity)
            )
        except TypeError:
            # Compatibility with an old extension that only supports positive
            # half-waves and returns (newX, newY, maxlen). Prefer rebuilding
            # utils.c; this branch preserves a useful fallback.
            if int(polarity) >= 0:
                try:
                    cross_x, cross_y, _maxlen = _rsfpy_utils.interp_cross(t32, amp32)
                    counts = np.full(amp32.shape[1], int(_maxlen), dtype=np.intp)
                except Exception as exc:
                    warnings.warn(
                        f"rsfpy_utils.interp_cross failed; falling back to Python: {exc}",
                        RuntimeWarning,
                    )
                    cross_x, cross_y, counts, _maxlen = _halfwave_cross_py(
                        t32, amp32, int(polarity)
                    )
            else:
                cross_x, cross_y, counts, _maxlen = _halfwave_cross_py(
                    t32, amp32, int(polarity)
                )
        except Exception as exc:
            warnings.warn(
                f"rsfpy_utils.interp_cross failed; falling back to Python: {exc}",
                RuntimeWarning,
            )
            cross_x, cross_y, counts, _maxlen = _halfwave_cross_py(
                t32, amp32, int(polarity)
            )
    else:
        cross_x, cross_y, counts, _maxlen = _halfwave_cross_py(
            t32, amp32, int(polarity)
        )

    return _polys_from_cross_arrays(
        cross_x=cross_x,
        cross_y=cross_y,
        counts=counts,
        trace_pos=trace_pos,
        transp=transp,
    )


def _prepare_xpos(
    xpos: Optional[ArrayLike],
    axis2: np.ndarray,
    n2: int,
) -> Tuple[np.ndarray, bool]:
    """
    Prepare one spatial baseline position per trace.

    xpos convention
    ---------------
    None:
        Use regular axis2 as trace positions.

    Provided xpos:
        Convert to numeric ndarray, flatten with C-order, then use the first n2
        values as trace positions. Time-varying / n1-varying xpos is not
        supported here. Any conversion failure, insufficient length, or NaN/Inf
        in the first n2 values raises ValueError.
    """
    axis2 = np.asarray(axis2, dtype=float).ravel()

    if xpos is None:
        if axis2.size != n2:
            raise ValueError(f"axis2 length must equal data.shape[1] ({n2}); got {axis2.size}.")
        if not np.all(np.isfinite(axis2)):
            raise ValueError("axis2 contains NaN or Inf values.")
        return axis2.astype(float, copy=True), False

    try:
        xflat = np.asarray(_as_plain_array(xpos), dtype=float).ravel()
    except Exception as exc:
        raise ValueError(
            "xpos must be convertible to a numeric array; it is flattened and "
            "the first data.shape[1] values are used as trace positions."
        ) from exc

    if xflat.size < n2:
        raise ValueError(
            f"flattened xpos must contain at least data.shape[1] ({n2}) values; "
            f"got {xflat.size}."
        )

    trace_pos = np.array(xflat[:n2], dtype=float, copy=True)
    if not np.all(np.isfinite(trace_pos)):
        raise ValueError("the first data.shape[1] values of xpos contain NaN or Inf.")

    return trace_pos, True


def wiggle(
    data: ArrayLike,
    *,
    xpos: Optional[ArrayLike] = None,
    d1: Optional[float] = None,
    o1: Optional[float] = None,
    d2: Optional[float] = None,
    o2: Optional[float] = None,
    min1: Optional[float] = None,
    max1: Optional[float] = None,
    min2: Optional[float] = None,
    max2: Optional[float] = None,
    label1: Optional[str] = None,
    label2: Optional[str] = None,
    title: Optional[str] = None,
    ax: Optional[plt.Axes] = None,
    ncolor: str = "blue",
    pcolor: str = "red",
    lcolor: str = "black",
    zplot: float = 1.0,
    transp: Optional[bool] = False,
    yreverse: Optional[bool] = False,
    xreverse: Optional[bool] = False,
    **plot_params: Optional[dict],
) -> plt.Axes:
    """
    Plot a 2D seismic wiggle image.

    Data convention
    ---------------
    data.shape == (n1, n2)
      n1: axis-1 samples, typically time/depth
      n2: traces

    xpos convention
    ---------------
    xpos=None:
      trace baselines are regular positions from axis2.

    xpos is provided:
      xpos is converted to numeric ndarray, flattened, and the first n2 values
      are used as trace baselines. This function does not support nonuniform
      sampling along n1/time for xpos.

    Range convention
    ----------------
    min1/max1 and min2/max2 always define final Matplotlib axis limits.
    For xpos provided, min2/max2 are applied to the flattened per-trace xpos.

    Extra plot_params
    -----------------
    clip : float or None
    bias : float
    allpos : bool
    pclip : percentile for clipping when clip is None
    linewidth : float
    use_c : bool, default True, use rsfpy_utils if available
    show : bool, default True
    """
    # Preserve metadata lookup before converting to a plain ndarray.
    data_obj = data

    if hasattr(data_obj, "d1"):
        d1 = d1 if d1 is not None else getattr(data_obj, "d1", None)
        d2 = d2 if d2 is not None else getattr(data_obj, "d2", None)
        o1 = o1 if o1 is not None else getattr(data_obj, "o1", None)
        o2 = o2 if o2 is not None else getattr(data_obj, "o2", None)
        if label1 is None and hasattr(data_obj, "label_unit"):
            label1 = data_obj.label_unit(axis=0)
        if label2 is None and hasattr(data_obj, "label_unit"):
            label2 = data_obj.label_unit(axis=1)

    arr = _as_plain_array(data_obj)
    if arr.ndim < 2:
        raise ValueError("Input data must be at least 2D.")
    if arr.ndim > 2:
        warnings.warn("Got data dimensions > 2, use first slice.", RuntimeWarning)
        arr = arr.reshape(arr.shape[0], arr.shape[1], -1)[:, :, 0]

    defaults = {
        "clip": None,
        "bias": 0.0,
        "allpos": False,
        "pclip": 99,
        "linewidth": 0.5,
        "use_c": True,
        "show": True,
        # Kept for backward compatibility. Actual fill uses zero-crossing polygons.
        "interpolate": False,
    }
    params = {**defaults, **plot_params}

    if ax is None:
        _fig, ax = plt.subplots()

    if d1 is None or d1 == 0:
        d1 = 1.0
    if d2 is None or d2 == 0:
        d2 = 1.0
    if o1 is None:
        o1 = 0.0
    if o2 is None:
        o2 = 0.0

    n1, n2 = arr.shape

    if hasattr(data_obj, "axis"):
        axis1, axis2 = data_obj.axis([0, 1])
        axis1 = np.asarray(axis1, dtype=float).ravel()
        axis2 = np.asarray(axis2, dtype=float).ravel()
    else:
        axis1 = np.arange(n1, dtype=float) * float(d1) + float(o1)
        axis2 = np.arange(n2, dtype=float) * float(d2) + float(o2)

    if axis1.size != n1:
        raise ValueError(f"axis1 length must equal data.shape[0] ({n1}); got {axis1.size}.")
    if axis2.size != n2:
        raise ValueError(f"axis2 length must equal data.shape[1] ({n2}); got {axis2.size}.")
    if not np.all(np.isfinite(axis1)):
        raise ValueError("axis1 contains NaN or Inf values.")

    trace_pos, use_xpos = _prepare_xpos(xpos=xpos, axis2=axis2, n2=n2)

    if min1 is None:
        min1 = float(np.nanmin(axis1)) if axis1.size else float(o1)
    if max1 is None:
        max1 = float(np.nanmax(axis1)) if axis1.size else float(o1)

    if min2 is None:
        min2 = float(np.nanmin(trace_pos)) if trace_pos.size else float(o2)
    if max2 is None:
        max2 = float(np.nanmax(trace_pos)) if trace_pos.size else float(o2)

    t = axis1

    clip = params["clip"]
    bias = float(params["bias"])
    pclip = params["pclip"]
    allpos = bool(params["allpos"])

    if clip is None:
        if pclip is not None:
            if hasattr(data_obj, "pclip"):
                clip = data_obj.pclip(pclip)
            else:
                clip = np.percentile(np.abs(arr), pclip)
        else:
            clip = np.nanmax(np.abs(arr))

    clip = abs(float(clip))
    if not np.isfinite(clip) or clip == 0.0:
        clip = 1.0

    if zplot == 0:
        zplot = 1.0

    default_amp = _default_trace_amp(trace_pos)
    scale = default_amp * abs(float(zplot)) / clip

    min2_use = min(float(min2), float(max2))
    max2_use = max(float(min2), float(max2))

    # Spatial selection is based on one xpos value per trace. min2/max2 still
    # define the final plotting window below.
    mask2 = (trace_pos >= min2_use) & (trace_pos <= max2_use)

    sel_trace_pos = trace_pos[mask2]
    sel_data = arr[:, mask2].astype(np.float32, copy=False) - np.float32(bias)
    nsel = sel_data.shape[1]

    if nsel > 0:
        if allpos:
            sel_data = np.maximum(sel_data, 0.0)

        amp = np.asfortranarray(sel_data * np.float32(scale), dtype=np.float32)
        wiggles = sel_trace_pos[None, :] + amp

        if lcolor != "none" and params["linewidth"] > 0:
            segs = _make_line_segments(t, wiggles, bool(transp))
            lc = LineCollection(segs, colors=lcolor, linewidths=params["linewidth"])
            ax.add_collection(lc)

        use_c = bool(params.get("use_c", True))

        if pcolor != "none":
            ppolys = _halfwave_polys(
                t=t,
                amp=amp,
                trace_pos=sel_trace_pos,
                polarity=1,
                transp=bool(transp),
                use_c=use_c,
            )
            if ppolys:
                pc = PolyCollection(ppolys, facecolors=pcolor, edgecolors="none")
                ax.add_collection(pc)

        if ncolor != "none" and not allpos:
            npolys = _halfwave_polys(
                t=t,
                amp=amp,
                trace_pos=sel_trace_pos,
                polarity=-1,
                transp=bool(transp),
                use_c=use_c,
            )
            if npolys:
                pc = PolyCollection(npolys, facecolors=ncolor, edgecolors="none")
                ax.add_collection(pc)

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

    # Final limits are always controlled by min1/max1 and min2/max2.
    if transp:
        if yreverse:
            ax.set_ylim(max1, min1)
        else:
            ax.set_ylim(min1, max1)
        if xreverse:
            ax.set_xlim(max2, min2)
        else:
            ax.set_xlim(min2, max2)
    else:
        if xreverse:
            ax.set_xlim(max1, min1)
        else:
            ax.set_xlim(min1, max1)
        if yreverse:
            ax.set_ylim(max2, min2)
        else:
            ax.set_ylim(min2, max2)

    if params.get("show", True):
        plt.show()

    return ax
