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



import io, re, warnings

def _check_input_source(src, mode='rb'):
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


def _str_match_re(str_in: str, pattern: str = r'\s+(?=(?:[^"]*"[^"]*")*[^"]*$)', strip: str | None = None) -> dict:
    """
    Match strings in the input using the specified regex pattern.
    """
    out_dict = {}
    tokens = re.split(pattern, str_in.strip(strip))
    for token in tokens:
        if "=" not in token:
            continue
        k, v = token.split("=", 1)
        out_dict[k] = v.strip('"').strip("'")
    return out_dict