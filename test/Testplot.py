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
import matplotlib.pyplot as plt

path = os.path.dirname(os.path.abspath(__file__))
sys.path.append(path + "/../src/")
from rsfpy.array import Rsfarray


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
    count, all = 0, 0
    verbose = False
    args = sys.argv[1:]
    if '-V' in args or '--version' in args:
        verbose = True
    if verbose: print("Test Rsfdata io...", file=file)
    # Read from file
    print(f"{all+1}:", end="\t", file=file)
    try:
        dat = Rsfarray(path + '/dat.test')
    except Exception as e:
        if verbose: print(color_str(f"Error reading Rsfarray from file {path + '/dat.test'}: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Reading RSF data file:                \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1
    
    print(f"{all+1}:", end="\t", file=file)
    try:
        dat.grey(show=False)
    except Exception as e:
        if verbose: print(color_str(f"Error generate grey plot: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Generating grey plot:                \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1    

    print(f"{all+1}:", end="\t", file=file)
    try:
        dat.wiggle(transp=True, yreverse=True,show=False, zplot=2.)
    except Exception as e:
        if verbose: print(color_str(f"Error generate wiggle plot: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Generating wiggle plot:                \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1

    print(f"{all+1}:", end="\t", file=file)
    n3 = 10
    stack = [dat * (1+ i3) for i3 in range(n3)]
    stack = np.stack(stack, axis=2)
    stack.sfput(label3='Scale', d3=1., o3=1., unit3='')
    try:
        stack.grey3(frame1=100, frame2=100, frame3=5, show=False)
    except Exception as e:
        if verbose: print(color_str(f"Error generate wiggle plot: {e}", 'red'), file=file)
        else: print(color_str(f'failed', 'red'), file=file)
    else:
        if verbose: print(f"Generating wiggle plot:                \t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1

    # Summary
    print(f"Summary:\t{all} tests, {(color_str(f'{count} passed', 'green'))}, {color_str(f'{all - count} failed', 'red' if all - count > 0 else 'green')}." , file=file)
    plt.show()
    sys.exit(0)



if __name__ == '__main__':
    main()
