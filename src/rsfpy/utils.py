import os
import io
import warnings

def check_input_source(src, mode='rb'):
    """
    Check if src is a valid readable/writable file path or IOBase object.
    """
    if isinstance(src, str):
        try:
            fp = open(src, mode)
            return fp
        except Exception as e:
            warnings.warn(f"File not accessible with {mode}: {src}, {e}")
            return None
    elif isinstance(src, io.IOBase):
        return src
    else:
        warnings.warn(f"Invalid input type: {type(src)}")
        return None
