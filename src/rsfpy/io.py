import numpy as np
import warnings, re, os
from .utils import check_input_source

RSFHSPLITER = b"\x0c\x0c\x04"

def read_rsf(file):
    """
    Read RSF file and return (data, header) or None.

    Parameters
    ----------
    file : str or file-like object
        The RSF file to read.

    Returns
    -------
    list [ndarray, dict, str]
        A list containing the read data, header, and history information.
    """
    try:
        close_after = isinstance(file, str)

        file_fp = check_input_source(file, 'rb')
        if file_fp is None:
            return None

        header = {}
        data = None

        # Read header
        buf = bytearray()
        while True:
            chunk = file_fp.read(1)
            if not chunk:
                break
            buf.extend(chunk)
            if buf.endswith(RSFHSPLITER):
                break

        header_text = buf.rstrip(RSFHSPLITER).decode("utf-8", errors="ignore")
        tokens = re.split(r"[\s\n\r]+", header_text.strip())
        for token in tokens:
            if "=" in token:
                k, v = token.split("=", 1)
                # 去掉首尾引号（双引号或单引号）
                v = v.strip('"').strip("'")
                header[k] = v  # 最后一次出现覆盖

        # Format conversion
        for k, v in list(header.items()):
            if re.fullmatch(r"n[1-9]", k) or k == "esize":
                try:
                    header[k] = int(v)
                except ValueError:
                    pass
            elif re.fullmatch(r"[od][1-9]", k):
                try:
                    header[k] = float(v)
                except ValueError:
                    pass

        # data source
        in_val = header.get("in", None)
        if in_val is None:
            warnings.warn("'in' key not found in RSF header")
            return None

        if in_val == "stdin":
            data_file = file_fp
        else:
            data_file = check_input_source(in_val, 'rb')
            if data_file is None:
                warnings.warn(f"Data file not accessible: {in_val}")
                return None

        # shape
        shape = []
        for i in range(1, 10):
            key = f"n{i}"
            if key in header:
                shape.append(int(header[key]))
            else:
                break
        if not shape:
            warnings.warn("No n# keys found for shape")
            return None

        # data_format
        fmt = header.get("data_format", None)
        if fmt is None:
            warnings.warn("'data_format' key not found")
            return None
        try:
            fmt_A, fmt_B = fmt.split("_", 1)
        except ValueError:
            warnings.warn(f"Invalid data_format: {fmt}")
            return None

        if fmt_A not in ("native", "ascii", "xdr"):
            warnings.warn(f"Unsupported format type: {fmt_A}")
            return None
        if fmt_B not in ("int", "float", "complex"):
            warnings.warn(f"Unsupported data type: {fmt_B}")
            return None

        dtype_map = {
            "int": np.int32,
            "float": np.float32,
            "complex": np.complex64
        }
        dtype = dtype_map[fmt_B]

        # 读取数据
        if fmt_A == "ascii":
            ascii_text = data_file.read().decode("utf-8", errors="ignore").strip()
            parts = ascii_text.split()
            if fmt_B == "complex":
                def parse_complex(s):
                    s = s.replace("i", "j")
                    return complex(s)
                arr = np.array([parse_complex(p) for p in parts], dtype=dtype)
            else:
                arr = np.array([float(p) for p in parts], dtype=dtype)
        else:
            arr = np.frombuffer(data_file.read(), dtype=dtype)
            if fmt_A == "xdr":
                arr = arr.byteswap().newbyteorder()

        arr = arr.reshape(shape)

        if in_val != "stdin":
            data_file.close()
        if close_after:
            file_fp.close()

        return [arr, header, header_text]

    except Exception as e:
        warnings.warn(f"Error reading RSF: {e}")
        return None



def write_rsf(file, header, data, out=None, form="native"):
    """
    Write RSF file with given header and data.
    """
    # TODO: integrate your latest write_rsf logic here
    pass
