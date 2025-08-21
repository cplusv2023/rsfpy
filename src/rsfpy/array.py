"""
  RsfPy - Python tools for Madagascar RSF data file reading/writing and scientific array handling.

  Copyright (C) 2025 Jilin University

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
"""



import numpy as np
import io, warnings
from .utils import _str_match_re
from .io import read_rsf, write_rsf

defaults = {
    "d1": 4.0e-3, "o1": 0., "label1": "Time", "unit1":"s",
    "d2": 8.0e-3, "o2": 0., "label2": "Distance", "unit2":"km",
    "d3": 8.0e-3, "o3": 0., "label3": "Distance", "unit3":"km",
    "d4": 1, "o4": 0.,
    "d5": 1, "o5": 0.,
    "d6": 1, "o6": 0.,
    "d7": 1, "o7": 0.,
    "d8": 1, "o8": 0.,
    "d9": 1, "o9": 0.,
}

class Rsfdata(np.ndarray):
    def __new__(cls, input_array: np.ndarray | None = None, header: dict | None = None, history: str = ""):
        """
         Ndarray wrapper for *Madagascar* RSF (regularly sampled format) data.
         RSF data format: https://www.ahay.org/wiki/Guide_to_RSF_file_format

         Parameters
         ----------
         input_array : None, str, file-like object, ndarray, or Rsfdata
             The input data to initialize the Rsfdata object.
         header : dict, optional
             Header information to associate with the data.
         history : str, optional
             History information to associate with the data.
        """
        # Check header and history format
        if header is None:
            header = {}
        if history is None:
            history = ""
        if not isinstance(header, dict):
            warnings.warn(f"Got invalid header format {type(header)}; expected a dictionary.")
            header = {}
        if not isinstance(history, str):
            warnings.warn(f"Got invalid history format {type(history)}; expected a string.")
            history = ""

        # Case 1: No input -> create empty array
        if input_array is None:
            obj = np.empty((0,), dtype=float).view(cls)
            obj.header = {}
            obj.history = ""
            return obj

        # Case 2: String -> file path or file-like object
        if isinstance(input_array, str) or isinstance(input_array, io.IOBase):
            obj = read_rsf(input_array)
            if isinstance(obj, list) and len(obj) == 3:
                obj = Rsfdata(*obj)
                obj.header.update(header)
                obj.history += '\n' + history
            else:
                warnings.warn(f"Failed to read RSF file: {input_array}")
                obj = Rsfdata()
            return obj

        # Case 3: Rsfdata
        if isinstance(input_array, Rsfdata):
            obj = np.asarray(input_array).view(cls)
            obj.header = dict(input_array.header)
            obj.history = str(input_array.history)
            obj.header.update(header)
            obj.history += '\n' + history
            return obj
        
        # Case 4: ndarray
        if isinstance(input_array, np.ndarray):
            obj = np.asarray(input_array).view(cls)
            obj.header = {} if header is None else header
            obj.history = history
            return obj

        raise TypeError(f"Unsupported input type: {type(input_array)}")

    def __array_finalize__(self, obj):
        """Ensure attributes are preserved in new views/slices."""
        if obj is None:
            return
        self.header = getattr(obj, 'header', {})
        self.history = getattr(obj, 'history', "")

        # Update n#
        self.update()

    def read(self, file: str | io.IOBase):
        """
        Read RSF data from a file or file-like object.
        This will override the existing data and header information in the array.
        Failure to read the data will not modify the array.

        Parameters
        ----------
        file : str or file-like object
            The RSF file to read.

        Returns
        -------
        Rsfdata
            A rsf data object (self) containing the read data and header information.
            Always returned as a valid object. Though the content may be None.
        """
        result = read_rsf(file)
        if isinstance(result, list) and len(result) == 3:
            self = Rsfdata(*result)

    def write(self, file: str | io.IOBase, **kargs):
        """
        Write RSF data to a file or file-like object.

        Parameters
        ----------
        file : str or file-like object
            The RSF file to write.
        header : dict
            The header information to write.
        out : str, optional
            The output file path (if different from `file`).
        form : str, optional
            The data format (default is "native").
            native: little-endian
            xdr: network (big-endian) byte order
            ascii: plain text
        """
        self.update(kargs.get('header', {}))
        history = self.history + '\n' + kargs.get('history', '')
        kargs.pop('header', None)
        kargs.pop('history', None)
        write_rsf(self, file, self.header, history, **kargs)

    def update(self, new_header: dict={}):
        """
        Update the header information.
        Ensure n# matches shape.

        Parameters
        ----------
        new_header : dict
            The new header information to update.
        """
        self.header.update(new_header)
        # Update n#
        for idim in range(self.ndim):
            n_key = f"n{idim + 1}"
            self.header.update({n_key: self.shape[idim]})
            if self.d(idim) == 0.:
                dkey = f"d{idim + 1}"
                self.header.update({dkey: defaults.get(dkey, 4.e-3)})


    def put(self, header_str: str = ''):
        """
        Modify header information.

        Parameters
        ----------
        header_str : str
            The header information to add or modify.
        """
        header = _str_match_re(header_str)
        self.update(header)

    def axis(self, axis: int = 0) -> np.ndarray:
        """
        Get the regular sampling of specific axis.

        Parameters
        ----------
        axis : int
            The axis along which to get the data.

        Returns
        -------
        np.ndarray
            The regular sampling of specific axis.
        """
        if axis < self.ndim:
            n = self.n(axis)
            o = self.o(axis)
            d = self.d(axis)
            return np.arange(n) * d + o

    def n(self, axis: int | list | tuple | np.ndarray = 0) -> int | tuple:
        """
        Get the number of samples along a specific axis.

        Parameters
        ----------
        axis : int | list | tuple | np.ndarray
            The axis along which to get the data.

        Returns
        -------
        int | tuple
            The number of samples along the specified axis.
        """
        if isinstance(axis, (list, tuple, np.ndarray)):
            axis = axis[:len(axis)] if len(axis) < self.ndim else axis[:self.ndim]
            return [self.header.get(f"n{ax+1}", 1 if self.data else 0) for ax in axis]
        return self.header.get(f"n{axis+1}", 1 if self.data else 0)

    def d(self, axis: int | list | tuple | np.ndarray = 0) -> float | tuple:
        """
        Get the sampling interval along a specific axis.

        Parameters
        ----------
        axis : int | list | tuple | np.ndarray
            The axis along which to get the data.

        Returns
        -------
        float | tuple
            The sampling interval along the specified axis.
        """
        if isinstance(axis, (list, tuple, np.ndarray)):
            axis = axis[:len(axis)] if len(axis) < self.ndim else axis[:self.ndim]
            return [float(self.header.get(f"d{ax+1}", defaults.get(f"d{ax+1}", 4.e-3))) for ax in axis]
        return float(self.header.get(f"d{axis+1}", defaults.get(f"d{axis+1}", 4.e-3)))

    def o(self, axis: int | list | tuple | np.ndarray = 0) -> float | tuple:
        """
        Get the offset along a specific axis.

        Parameters
        ----------
        axis : int | list | tuple | np.ndarray
            The axis along which to get the data.

        Returns
        -------
        float | tuple
            The offset along the specified axis.
        """
        if isinstance(axis, (list, tuple, np.ndarray)):
            axis = axis[:len(axis)] if len(axis) < self.ndim else axis[:self.ndim]
            return [self.header.get(f"o{ax+1}", defaults.get(f"o{ax+1}", 0.0)) for ax in axis]
        return self.header.get(f"o{axis+1}", defaults.get(f"o{axis+1}", 0.0))

    def label(self, axis: int | list | tuple | np.ndarray = 0) -> str | tuple:
        """
        Get the label along a specific axis.

        Parameters
        ----------
        axis : int | list | tuple | np.ndarray
            The axis along which to get the data.

        Returns
        -------
        str | tuple
            The label along the specified axis.
        """
        if isinstance(axis, (list, tuple, np.ndarray)):
            axis = axis[:len(axis)] if len(axis) < self.ndim else axis[:self.ndim]
            return [self.header.get(f"label{ax+1}", defaults.get(f"label{ax+1}", "")) for ax in axis]
        return self.header.get(f"label{axis+1}", defaults.get(f"label{axis+1}", ""))

    def unit(self, axis: int | list | tuple | np.ndarray = 0) -> str | tuple:
        """
        Get the unit along a specific axis.

        Parameters
        ----------
        axis : int | list | tuple | np.ndarray
            The axis along which to get the data.

        Returns
        -------
        str | tuple
            The unit along the specified axis.
        """
        if isinstance(axis, (list, tuple, np.ndarray)):
            axis = axis[:len(axis)] if len(axis) < self.ndim else axis[:self.ndim]
            return [self.header.get(f"unit{ax+1}", defaults.get(f"unit{ax+1}", "")) for ax in axis]
        return self.header.get(f"unit{axis+1}", defaults.get(f"unit{axis+1}", ""))

    def label_unit(self, axis: int | list | tuple | np.ndarray = 0) -> str | tuple:
        """
        Get the 'label (unit)' along a specific axis.

        Parameters
        ----------
        axis : int | list | tuple | np.ndarray
            The axis along which to get the data.

        Returns
        -------
        str | tuple
            The 'label (unit)' along the specified axis.
        """
        label = self.label(axis)
        unit = self.unit(axis)
        labels = ()
        if isinstance(label, (list, tuple, np.ndarray)):
            for l, u in zip(label, unit):
                labels += (f"{l} ({u})",) if u else (l,)
            return labels
        if unit:
            return f"{label} ({unit})"
        return label

    def transpose(self, axes: tuple | list =None):
        """
        Transpose array and update RSF header accordingly
        (n#, o#, d#, label#, unit# will be permuted with axes).
        
        Parameters
        ----------
        axes : tuple or list of ints, optional
            By default, reverse the dimensions (same as .T)
        """
        if axes is None:
            axes = tuple(reversed(range(self.ndim)))
        else:
            axes = tuple(axes)

        obj = super().transpose(axes)

        keys_per_dim = ("n", "o", "d", "label", "unit")
        new_header = {}

        for new_axis, old_axis in enumerate(axes, start=1):
            old_idx = old_axis + 1
            for ktype in keys_per_dim:
                old_key = f"{ktype}{old_idx}"
                new_key = f"{ktype}{new_axis}"
                if old_key in self.header:
                    new_header[new_key] = self.header[old_key]

        for k, v in self.header.items():
            if not any(k.startswith(p) and k[len(p):].isdigit() for p in keys_per_dim):
                new_header[k] = v

        obj.header = new_header
        return obj
    
    @property
    def T(self):
        """
        Transpose like NumPy with header synchronization.
        """
        return self.transpose()
    
    def window(self, cmd=None, **kwargs):
        """
        Apply simple windowing with only n#, j#, f# parameters.

        Parameters
        ----------
        cmd : str or None
            Command-line style, e.g. 'n1=100 j1=2'.
        n# : int
            Number of samples along axis #.
        j# : int
            Jump factor along axis #.
        f# : int
            First sample along axis #.

        Returns
        -------
        Rsfdata
            Windowed data with updated metadata.
        """
        def _parse_cmd_string(cmd_str):
            params = {}
            for token in cmd_str.strip().split():
                if '=' in token:
                    k, v = token.split('=', 1)
                    try:
                        params[k.strip()] = float(v) if '.' in v or 'e' in v.lower() else int(v)
                    except ValueError:
                        params[k.strip()] = v
            return params

        if cmd is not None:
            raw_params = _parse_cmd_string(cmd)
        else:
            raw_params = dict(kwargs)

        nd = self.ndim
        params = {f'n{i+1}': None for i in range(nd)}
        params.update({f'f{i+1}': None for i in range(nd)})
        params.update({f'j{i+1}': None for i in range(nd)})

        for k, v in raw_params.items():
            if k in params:
                params[k] = v

        new_meta = self.header.copy()

        for ax in range(nd):
            n = params[f'n{ax+1}'] if params[f'n{ax+1}'] is not None else self.n(ax)
            j = params[f'j{ax+1}'] if params[f'j{ax+1}'] is not None else 1
            f = params[f'f{ax+1}'] if params[f'f{ax+1}'] is not None else 0
            if n < 0: n = self.n(ax)
            if f < 0: f += self.n(ax)
            slices = np.arange(f, f + n * j, j, dtype=int)
            slices = slices[np.where((slices >= 0) & (slices < self.n(ax)))]
            self = np.take(self, slices, axis=ax)

            new_meta[f'n{ax+1}'] = len(slices)
            new_meta[f'o{ax+1}'] = f * self.d(ax) + self.o(ax)
            new_meta[f'd{ax+1}'] = (self.d(ax) * j)

        self.header = new_meta
        return self

    def flip(self, axis: int = 0):
        """
        Flip the array along the specified axis.

        Parameters
        ----------
        axis : int
            The axis along which to flip the array.
        """
        self = np.flip(self, axis=axis)
        o = self.header.get(f'o{axis+1}', 0.0)
        d = self.header.get(f'd{axis+1}', 4.0e-3)
        n = self.header.get(f'n{axis+1}', 1)

        self.header[f'o{axis+1}'] = o + (n-1)*d
        self.header[f'd{axis+1}'] = -d

        return self

    def pclip(self, perc: float=99.)-> int | float | None:
        """
        Caculate the percentile clipping values.

        Parameters
        ----------
        perc : float
            The percentile value to clip the data.

        Returns
        -------
        int | float | None
            The clipped data value.
        """
        if not np.issubdtype(self.dtype, np.integer) and not np.issubdtype(self.dtype, np.floating):
            return None
        if not 0 <= perc <= 100:
            warnings.warn("Clip percentile must be between 0 and 100. Use default pclip=99.")
        # Compute the clipping values
        clip = np.percentile(self, perc)
        return clip

# add some properties for convenience
for idim in range(9):
    setattr(Rsfdata, f'n{idim+1}', property(lambda self, i=idim: self.n(i)))
    setattr(Rsfdata, f'o{idim+1}', property(lambda self, i=idim: self.o(i)))
    setattr(Rsfdata, f'd{idim+1}', property(lambda self, i=idim: self.d(i)))
    setattr(Rsfdata, f'label{idim+1}', property(lambda self, i=idim: self.label(i)))
    setattr(Rsfdata, f'unit{idim+1}', property(lambda self, i=idim: self.unit(i)))
    setattr(Rsfdata, f'axis{idim+1}', property(lambda self, i=idim: self.axis(i)))
setattr(Rsfdata, f'clip', property(lambda self: self.pclip()))


class Rsfarray(np.ndarray):
    """
    Deprecated alias for Rsfdata.
    """
    def __new__(cls, *args, **kwargs):
        warnings.warn("Rsfarray is deprecated, use Rsfdata instead.", DeprecationWarning)
        return Rsfdata(*args, **kwargs)
    
    def __array_finalize__(self, obj):
        if obj is None:
            return
        self.header = getattr(obj, 'header', {})
        self.history = getattr(obj, 'history', "")