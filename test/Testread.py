import sys, os
import numpy as np 

path = os.path.dirname(os.path.abspath(__file__))
sys.path.append(path + "/../src/")
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
        if verbose: print(f"Empty Rsfdata creation:\t{color_str('passed', 'green')}.", file=file)
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
        if verbose: print(f"Rsfdata created from ndarray:\t{color_str('passed', 'green')}.", file=file)
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
        if verbose: print(f"Rsfdata file reading:\t{color_str('passed', 'green')}.", file=file)
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
        if verbose: print(f"Rsfdata created from Rsfdata:\t{color_str('passed', 'green')}.", file=file)
        else: print(color_str(f'passed', 'green'), file=file)
        count += 1
    all += 1
    # Summary
    print(f"Summary:\t{all} tests {(color_str(f'{count} passed', 'green'))}, {color_str(f'{all - count} failed', 'red')}.", file=file)

    if all - count > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()