from __future__ import annotations

"""Simple FFT/IFFT helpers for Rsfarray-like ndarray subclasses.

Rules implemented
-----------------
1. ``rfft`` determines whether to use real FFT or full FFT. No guessing.
2. Full FFT is always shifted (``fftshift`` / ``ifftshift``).
3. Forward transform stores original axis metadata in header:
   ``fft_n#``, ``fft_o#``, ``fft_d#``, ``fft_label#``, ``fft_unit#``,
   ``fft_type#``.
4. Inverse transform restores from header when available; otherwise it infers
   a reasonable sampling from the current frequency spacing.
5. Forward transformed axis is always labeled ``Frequency`` / ``Hz``.
6. ``sym`` maps to NumPy ``norm``: ``False -> 'backward'``,
   ``True -> 'ortho'``.
7. Dtypes are normalized to ``float32`` / ``complex64``.

The module works with
- plain ``numpy.ndarray``
- ndarray subclasses carrying a ``header`` attribute (such as ``Rsfarray``)

For plain ndarrays, the module returns a lightweight ndarray subclass with a
``header`` attribute.
"""

from typing import Any, Dict, Iterable, Union

import numpy as np


__all__ = ["fft", "ifft"]


# -----------------------------------------------------------------------------
# Internal helpers
# -----------------------------------------------------------------------------

def _normalize_axis(axis: int, ndim: int) -> int:
    if not -ndim <= axis < ndim:
        raise np.AxisError(axis, ndim=ndim)
    return axis % ndim



def _norm_from_sym(sym: bool) -> str:
    return "ortho" if sym else "backward"



def _is_complex_dtype(dtype: np.dtype) -> bool:
    return np.issubdtype(dtype, np.complexfloating)



def _is_real_dtype(dtype: np.dtype) -> bool:
    return np.issubdtype(dtype, np.floating)



def _to_work_dtype(a: np.ndarray, *, require_real: bool = False) -> np.ndarray:
    if require_real:
        if _is_complex_dtype(a.dtype):
            raise TypeError("rfft=True requires a real-valued input array.")
        return np.asarray(a, dtype=np.float32)

    if _is_complex_dtype(a.dtype):
        return np.asarray(a, dtype=np.complex64)
    if _is_real_dtype(a.dtype):
        return np.asarray(a, dtype=np.float32)
    return np.asarray(a, dtype=np.float32)



def _extract_header(data: np.ndarray) -> Dict[str, Any]:
    header = getattr(data, "header", None)
    if isinstance(header, dict):
        return header.copy()
    return {}



def _default_n(shape: Iterable[int], i: int) -> int:
    return int(tuple(shape)[i])



def _header_get_n(header: Dict[str, Any], shape: tuple[int, ...], i: int) -> int:
    return int(header.get(f"n{i+1}", shape[i]))



def _header_get_o(header: Dict[str, Any], i: int) -> float:
    return float(header.get(f"o{i+1}", 0.0))



def _header_get_d(header: Dict[str, Any], i: int) -> float:
    return float(header.get(f"d{i+1}", 1.0))



def _header_get_label(header: Dict[str, Any], i: int) -> str:
    val = header.get(f"label{i+1}", "")
    return "" if val is None else str(val)



def _header_get_unit(header: Dict[str, Any], i: int) -> str:
    val = header.get(f"unit{i+1}", "")
    return "" if val is None else str(val)



def _set_axis_meta(
    header: Dict[str, Any],
    i: int,
    *,
    n: int,
    o: float,
    d: float,
    label: str | None = None,
    unit: str | None = None,
) -> None:
    idx = i + 1
    header[f"n{idx}"] = int(n)
    header[f"o{idx}"] = float(o)
    header[f"d{idx}"] = float(d)
    if label is not None:
        header[f"label{idx}"] = label
    if unit is not None:
        header[f"unit{idx}"] = unit



def _sync_header_shape(header: Dict[str, Any], shape: tuple[int, ...]) -> None:
    ndim = len(shape)
    for i, n in enumerate(shape, start=1):
        header[f"n{i}"] = int(n)
        header.setdefault(f"o{i}", 0.0)
        header.setdefault(f"d{i}", 1.0)
    # Trim trailing axis metadata above ndim to avoid stale sizes.
    for i in range(ndim + 1, 10):
        header.pop(f"n{i}", None)
        header.pop(f"o{i}", None)
        header.pop(f"d{i}", None)
        header.pop(f"label{i}", None)
        header.pop(f"unit{i}", None)



def _wrap_like(data: np.ndarray, out: np.ndarray, header: Dict[str, Any]) -> np.ndarray:
    out_arr = np.asarray(out)
    if isinstance(data, np.ndarray) and type(data) is not np.ndarray:
        try:
            wrapped = out_arr.view(type(data))
            setattr(wrapped, "header", header)
            return wrapped
        except Exception:
            pass
    return Rsfarray(out_arr, header=header)



def _backup_axis_meta(header: Dict[str, Any], axis: int, shape: tuple[int, ...]) -> None:
    idx = axis + 1
    header[f"fft_n{idx}"] = int(_header_get_n(header, shape, axis))
    header[f"fft_o{idx}"] = float(_header_get_o(header, axis))
    header[f"fft_d{idx}"] = float(_header_get_d(header, axis))
    header[f"fft_label{idx}"] = _header_get_label(header, axis)
    header[f"fft_unit{idx}"] = _header_get_unit(header, axis)



def _forward_freq_axis(n: int, d: float, *, rfft: bool) -> tuple[int, float, float]:
    if d == 0.0:
        raise ValueError("Sampling interval d must be non-zero for FFT.")

    df = 1.0 / (float(n) * float(d))

    if rfft:
        n_new = n // 2 + 1
        o_new = 0.0
        d_new = df
        return n_new, o_new, d_new

    # full FFT with shift
    n_new = n
    if n % 2 == 0:
        o_new = -(n // 2) * df
    else:
        o_new = -((n - 1) // 2) * df
    d_new = df
    return n_new, o_new, d_new



def _inverse_restore_axis(
    header: Dict[str, Any],
    axis: int,
    *,
    freq_len: int,
    freq_d: float,
    rfft: bool,
) -> tuple[int, float, float, str, str]:
    idx = axis + 1

    if f"fft_n{idx}" in header:
        n = int(header[f"fft_n{idx}"])
        o = float(header.get(f"fft_o{idx}", 0.0))
        d = float(header.get(f"fft_d{idx}", 1.0))
        label = str(header.get(f"fft_label{idx}", ""))
        unit = str(header.get(f"fft_unit{idx}", ""))
        return n, o, d, label, unit

    # Auto-infer when backup metadata are absent.
    if rfft:
        n = 2 * (freq_len - 1)
    else:
        n = freq_len

    if freq_d == 0.0:
        d = 1.0
    else:
        d = 1.0 / (float(n) * float(freq_d))
    o = 0.0
    label = ""
    unit = ""
    return n, o, d, label, unit



def _validate_inverse_input(data: np.ndarray, axis: int, rfft: bool, header: Dict[str, Any]) -> None:
    if not _is_complex_dtype(np.asarray(data).dtype):
        raise TypeError("ifft() expects a complex-valued input spectrum.")

    if rfft:
        idx = axis + 1
        recorded = header.get(f"fft_type{idx}", None)
        if recorded is not None and recorded != "rfft":
            raise ValueError(
                f"Axis {idx} was recorded as fft_type={recorded!r}, not 'rfft'."
            )
    else:
        idx = axis + 1
        recorded = header.get(f"fft_type{idx}", None)
        if recorded is not None and recorded != "fft":
            raise ValueError(
                f"Axis {idx} was recorded as fft_type={recorded!r}, not 'fft'."
            )


# -----------------------------------------------------------------------------
# Public API
# -----------------------------------------------------------------------------

def fft(data: Union[np.ndarray, "Rsfarray"], axis: int = 0, sym: bool = False, rfft: bool = True) -> np.ndarray:
    """FFT along one axis, returning an Rsfarray-like object.

    Parameters
    ----------
    data
        ``numpy.ndarray`` or Rsfarray-like ndarray subclass.
    axis
        Axis to transform. Negative axes are supported.
    sym
        If ``True``, use symmetric normalization (NumPy ``norm='ortho'``).
        Otherwise use NumPy default backward normalization.
    rfft
        If ``True``, perform real FFT and require real input.
        If ``False``, perform full FFT and always apply ``fftshift``.
    """
    arr0 = np.asarray(data)
    if arr0.ndim == 0:
        raise ValueError("fft() requires an array with ndim >= 1.")

    axis = _normalize_axis(axis, arr0.ndim)
    header = _extract_header(data)
    _sync_header_shape(header, arr0.shape)

    norm = _norm_from_sym(sym)
    arr = _to_work_dtype(arr0, require_real=rfft)

    _backup_axis_meta(header, axis, arr.shape)

    if rfft:
        spec = np.fft.rfft(arr, axis=axis, norm=norm)
        spec = np.asarray(spec, dtype=np.complex64)
    else:
        spec = np.fft.fft(arr, axis=axis, norm=norm)
        spec = np.fft.fftshift(spec, axes=axis)
        spec = np.asarray(spec, dtype=np.complex64)

    n_old = int(arr.shape[axis])
    d_old = _header_get_d(header, axis)
    n_new, o_new, d_new = _forward_freq_axis(n_old, d_old, rfft=rfft)

    _set_axis_meta(
        header,
        axis,
        n=n_new,
        o=o_new,
        d=d_new,
        label="Frequency",
        unit="Hz",
    )
    header[f"fft_type{axis+1}"] = "rfft" if rfft else "fft"
    _sync_header_shape(header, spec.shape)

    return _wrap_like(data, spec, header)



def ifft(data: Union[np.ndarray, "Rsfarray"], axis: int = 0, sym: bool = False, rfft: bool = True) -> np.ndarray:
    """Inverse FFT along one axis, returning an Rsfarray-like object.

    Parameters
    ----------
    data
        Complex spectrum as ``numpy.ndarray`` or Rsfarray-like ndarray subclass.
    axis
        Axis to transform. Negative axes are supported.
    sym
        If ``True``, use symmetric normalization (NumPy ``norm='ortho'``).
        Otherwise use NumPy default backward normalization.
    rfft
        If ``True``, input must be an ``rfft`` half-spectrum.
        If ``False``, input must be a full shifted spectrum.
    """
    arr0 = np.asarray(data)
    if arr0.ndim == 0:
        raise ValueError("ifft() requires an array with ndim >= 1.")

    axis = _normalize_axis(axis, arr0.ndim)
    header = _extract_header(data)
    _sync_header_shape(header, arr0.shape)
    _validate_inverse_input(arr0, axis, rfft, header)

    norm = _norm_from_sym(sym)
    arr = _to_work_dtype(arr0, require_real=False)

    idx = axis + 1
    freq_len = int(arr.shape[axis])
    freq_d = _header_get_d(header, axis)
    n_out, o_out, d_out, label_out, unit_out = _inverse_restore_axis(
        header,
        axis,
        freq_len=freq_len,
        freq_d=freq_d,
        rfft=rfft,
    )

    if rfft:
        out = np.fft.irfft(arr, n=n_out, axis=axis, norm=norm)
        out = np.asarray(out, dtype=np.float32)
    else:
        arr = np.fft.ifftshift(arr, axes=axis)
        out = np.fft.ifft(arr, axis=axis, norm=norm)
        out = np.asarray(out, dtype=np.complex64)

    _set_axis_meta(
        header,
        axis,
        n=n_out,
        o=o_out,
        d=d_out,
        label=label_out,
        unit=unit_out,
    )

    # Remove/refresh transform-state metadata for this axis after inverse.
    for key in (
        f"fft_n{idx}",
        f"fft_o{idx}",
        f"fft_d{idx}",
        f"fft_label{idx}",
        f"fft_unit{idx}",
        f"fft_type{idx}",
    ):
        header.pop(key, None)

    _sync_header_shape(header, out.shape)
    return _wrap_like(data, out, header)
