import numpy as np
import os, io, warnings
from .io import read_rsf

class Rsfdata(np.ndarray):
    def __new__(cls, input_array=None, header=None, history=""):
        """
         Ndarray wrapper for Madagascar RSF data.
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
        for idim in range(self.ndim):
            n_key = f"n{idim + 1}"
            self.header.update({n_key: self.shape[idim]})

    def read(self, file):
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
            result = Rsfdata(*result)
            self[:] = result.data
            self.header = result.header
            self.history = result.history
        return self
    
    def axis(self, axis: int)-> np.ndarray:
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
            n = self.header.get(f"n{axis+1}", 0)
            o = self.header.get(f"o{axis+1}", 0.0)
            d = self.header.get(f"d{axis+1}", 1.0e-4)
            return np.arange(n) * d + o
    
    def transpose(self, axes=None):
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

        # 先用 ndarray 的 transpose 得到新对象
        obj = super().transpose(axes)

        # ---- 更新 header ----
        keys_per_dim = ("n", "o", "d", "label", "unit")
        new_header = {}

        for new_axis, old_axis in enumerate(axes, start=1):
            old_idx = old_axis + 1
            for ktype in keys_per_dim:
                old_key = f"{ktype}{old_idx}"
                new_key = f"{ktype}{new_axis}"
                if old_key in self.header:
                    new_header[new_key] = self.header[old_key]

        # 保留其它非维度信息
        for k, v in self.header.items():
            if not any(k.startswith(p) and k[len(p):].isdigit() for p in keys_per_dim):
                new_header[k] = v

        obj.header = new_header
        return obj
    
    @property
    def T(self):
        """像 NumPy 一样的转置快捷属性，但会同步 header 元数据"""
        return self.transpose()
    
    def window(self, cmd=None, **kwargs):
        """
        Apply simple windowing with only n#, j#, f# parameters.

        Parameters
        ----------
        cmd : str or None
            Command-line style, e.g. 'n1=100 j1=2'.
        **kwargs : dict
            Keyword-style, e.g. n1=100, j1=2.

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

        # 覆盖填充
        for k, v in raw_params.items():
            if k in params:
                params[k] = v

        new_meta = self.header.copy()

        for ax in range(nd):
            n = params[f'n{ax+1}'] if params[f'n{ax+1}'] is not None else self.shape[ax]
            j = params[f'j{ax+1}'] if params[f'j{ax+1}'] is not None else 1
            f = params[f'f{ax+1}'] if params[f'f{ax+1}'] is not None else 0
            if n < 0: n = self.shape[ax] 
            if f < 0: f += self.shape[ax]
            slices = np.arange(f, f + n * j, j, dtype=int)
            slices = slices[np.where((slices >= 0) & (slices < self.shape[ax]))]
            self = np.take(self, slices, axis=ax)

            new_meta[f'n{ax+1}'] = len(slices)
            new_meta[f'o{ax+1}'] = f * self.header.get(f'd{ax+1}', 4.0e-3) + self.header.get(f'o{ax+1}', 0.0)
            new_meta[f'd{ax+1}'] = (self.header.get(f'd{ax+1}', 4.0e-3) * j)

        self.header = new_meta
        return self
    
    def flip(self, axis=0):
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