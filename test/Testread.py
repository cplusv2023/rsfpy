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


import sys, os, io
import numpy as np 

path = os.path.dirname(os.path.abspath(__file__))
# sys.path.append(path + "/../src/")
from rsfpy.array import Rsfdata


def color_str(string, color='green'):
    
    colors = {
        'green': "\033[92m",
        'red': "\033[91m",
        'yellow': "\033[93m",
        'blue': "\033[94m",
        'magenta': "\033[95m",
        'cyan': "\033[96m",
        'white': "\033[97m",
    }
    return f"{colors.get(color, colors['green'])}{string}\033[0m"

def main(file=sys.stderr):
# read args from command line
    count, all = 0, 0
    verbose = False
    args = sys.argv[1:]
    if '-V' in args or '--version' in args:
        verbose = True
    if verbose: print("Test Rsfdata io...", file=file)
    # None test
    print(f"{all+1}:", end="\t", file=file)
    try:
        dat = Rsfdata()
    except Exception as e:
        if verbose: print(color_str(f"Error creating empty Rsfdata: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Empty Rsfdata creation:                \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1
    # ndarray test
    print(f"{all+1}:", end="\t", file=file)
    try:
        arr = np.array([1, 2, 3])
        dat = Rsfdata(arr, header={"n1":3}, history="test")
    except Exception as e:
        if verbose: print(color_str(f"Error creating Rsfdata from ndarray: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Rsfdata created from ndarray:        \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1

    # file test
    print(f"{all+1}:", end="\t", file=file)
    try:
        # locate directory of __file__
        dat = Rsfdata(path + "/dat.test")
    except Exception as e:
        if verbose: print(color_str(f"Error creating Rsfdata from file: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Rsfdata file reading:                \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1

    # Rsfdata test
    print(f"{all+1}:", end="\t", file=file)
    try:
        dat = Rsfdata(dat)
    except Exception as e:
        if verbose: print(color_str(f"Error creating Rsfdata from Rsfdata: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Rsfdata created from Rsfdata:        \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1


    # Write to BytesIO
    print(f"{all+1}:", end="\t", file=file)
    file_io = io.BytesIO()
    try:
        dat.write(file_io, header={"n1":3}, history="test write")
    except Exception as e:
        if verbose: print(color_str(f"Error writing Rsfdata to file: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Rsfdata file writing to BytesIO:\t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1

    # Write to file
    print(f"{all+1}:", end="\t", file=file)
    file_io = path + "/dat.test.write"
    try:
        dat.write(file_io, header={"n1":3}, history="test write", form='xdr', fmt="%e")
    except Exception as e:
        if verbose: print(color_str(f"Error writing Rsfdata to file: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Rsfdata file writing to file:        \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1
    print("?",file=file)
    dat.put("d1=0.000")
    print(dat.d1, file=file)
    # Summary
    print(f"Summary:\t{all} tests {(color_str(f'{count} passed', 'green'))}, {color_str(f'{all - count} failed', 'red')}.", file=file)

    if all - count > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()