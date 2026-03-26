#!/usr/bin/env python3
# -*- coding: utf-8 -*-


__doc__ = """
\033[1mNAME\033[0m
    \tMrsfmath.py

\033[1mDESCRIPTION\033[0m
    \tapply NumPy/Python expressions or sequential commands to RSF data.
    \tsupport expression mode and multi-command mode:
    \t\033[1moutput=\033[0m\tevaluate a Python expression and write it as output
    \t\033[1mcmd=\033[0m\tfirst command to execute
    \t\033[1mcmd0=, cmd1=, cmd2=, ...\033[0m\tsubsequent commands executed in order

\033[1mSYNOPSIS\033[0m
    \tMrsfmath.py [options] < input.rsf > output.rsf
    \tMrsfmath.py < in.rsf other=other.rsf output="np.add(input,other)"
    \tMrsfmath.py < in.rsf other=other.rsf cmd="c=np.add(input,other)" cmd0="output=c.transpose([1,0])"
    \tMrsfmath.py n1=100 n2=50 d1=0.004 o1=0.0 output="np.sin(x1)[:,None]*np.ones((n1,n2),dtype=np.float32)" > out.rsf

\033[1mCOMMENTS\033[0m
    \tInput data is read from stdin as the main RSF variable \033[1minput\033[0m.

    \tAdditional input files can be provided as key=value pairs, for example:
    \t\tother=other.rsf
    \twhich will create a variable named \033[1mother\033[0m in the execution environment.

    \tThe following variables are available in cmd=/output=:
    \t\t\033[1minput\033[0m          main input Rsfarray
    \t\t\033[1mnp\033[0m             NumPy module
    \t\t\033[1mRsfarray\033[0m       Rsfarray class
    \t\t\033[1mx1,x2,x3,...\033[0m   coordinate vectors of main input
    \t\t\033[1mn1,n2,n3,...\033[0m   dimensions of main input
    \t\t\033[1mo1,o2,o3,...\033[0m   origins of main input
    \t\t\033[1md1,d2,d3,...\033[0m   samplings of main input

    \tFor extra input variable \033[1mother\033[0m, corresponding axis variables are also injected:
    \t\t\033[1mother_x1, other_x2, ...\033[0m
    \t\t\033[1mother_n1, other_n2, ...\033[0m
    \t\t\033[1mother_o1, other_o2, ...\033[0m
    \t\t\033[1mother_d1, other_d2, ...\033[0m

    \tFor Rsfarray objects, axis vectors can also be accessed directly as:
    \t\t\033[1minput.axis1, input.axis2, input.axis3, ...\033[0m
    \t(note that axis numbering starts from 1, not 0).

    \tCommands are executed in this order:
    \t\t\033[1mcmd\033[0m -> \033[1mcmd0\033[0m -> \033[1mcmd1\033[0m -> \033[1mcmd2\033[0m -> ...

    \tIf \033[1moutput=\033[0m is given, it is evaluated after all commands have been executed.

    \tIf \033[1moutput=\033[0m is not given, then one of the commands must assign the final result to
    \tvariable \033[1moutput\033[0m.

    \tIf stdin is empty, a synthetic zero array can be created by specifying
    \t\033[1mn1=, n2=, ...\033[0m and optional header keys such as \033[1mo1=, d1=, label1=, unit1=\033[0m.

\033[1mPARAMETERS\033[0m
    \t\033[4mstring\033[0m\t\033[1moutput=\033[0m Python expression to evaluate as final output
    \t\033[4mstring\033[0m\t\033[1mcmd=\033[0m first Python command to execute
    \t\033[4mstring\033[0m\t\033[1mcmd0=\033[0m second Python command to execute
    \t\033[4mstring\033[0m\t\033[1mcmd1=\033[0m third Python command to execute
    \t\033[4mstring\033[0m\t\033[1mcmd2=\033[0m fourth Python command to execute
    \t\033[4mstring\033[0m\t\033[1m...\033[0m continue as needed with cmd3, cmd4, ...

    \t\033[4mint\033[0m\t\033[1mn1,n2,n3,...\033[0m shape of synthetic input when stdin is absent
    \t\033[4mfloat\033[0m\t\033[1mo1,o2,o3,...\033[0m origin(s) of synthetic input
    \t\033[4mfloat\033[0m\t\033[1md1,d2,d3,...\033[0m sampling interval(s) of synthetic input
    \t\033[4mstring\033[0m\t\033[1mlabel1,label2,label3,...\033[0m axis label(s) of synthetic input
    \t\033[4mstring\033[0m\t\033[1munit1,unit2,unit3,...\033[0m axis unit(s) of synthetic input

    \t\033[4mfile\033[0m\t\033[1mother=other.rsf\033[0m load an additional RSF file as variable \033[1mother\033[0m
    \t\033[4mfile\033[0m\t\033[1mb=bbb.rsf\033[0m load an additional RSF file as variable \033[1mb\033[0m
    \t\033[4mfile\033[0m\t\033[1mname=file.rsf\033[0m any valid identifier can be used as an extra input variable name

\033[1mEXAMPLES\033[0m
    \t1. Element-wise addition:
    \t\tMrsfmath.py < in.rsf other=other.rsf output="np.add(input,other)" > out.rsf

    \t2. Multiple commands:
    \t\tMrsfmath.py < in.rsf other=other.rsf \\
    \t\tcmd="c=np.add(input,other)" \\
    \t\tcmd0="d=c.transpose([1,0])" \\
    \t\tcmd1="output=d" > out.rsf

    \t3. Use coordinate vector x1:
    \t\tMrsfmath.py < in.rsf output="a*np.cos(x1)[:,None]" > out.rsf

    \t4. Use Rsfarray axis directly:
    \t\tMrsfmath.py < in.rsf output="a*np.exp(-(a.axis1**2))[:,None]" > out.rsf

    \t5. Create synthetic input without stdin:
    \t\tMrsfmath.py n1=200 n2=50 d1=0.004 o1=0.0 \\
    \t\toutput="np.sin(x1)[:,None]*np.ones((n1,n2),dtype=np.float32)" > out.rsf

\033[1mNOTES\033[0m
    \t\033[1moutput=\033[0m should be a Python expression.

    \t\033[1mcmd, cmd0, cmd1, ...\033[0m are executed as Python statements.

    \tThis program uses Python exec/eval internally, so it is intended for trusted input.

    \tExtra input variable names must be valid Python identifiers.

\033[1mMORE INFO\033[0m
    \tAuthor:\tauthor_label
    \tEmail:\temail_label
    \tSource:\tgithub_label

\033[1mVERSION\033[0m
    \tversion_label
"""

import sys, os, subprocess
import re
from textwrap import dedent
from typing import Optional, Union
import numpy as np

from rsfpy import Rsfarray
from rsfpy.version import __version__, __email__, __author__, __github__

__progname__ = os.path.basename(sys.argv[0])

__doc__ = __doc__.replace("author_label", __author__)
__doc__ = __doc__.replace("email_label", __email__)
__doc__ = __doc__.replace("github_label", __github__)
__doc__ = __doc__.replace("version_label", __version__)

DOC = dedent(__doc__.replace("Mrsfmath.py", __progname__))

USAGE = DOC


def sf_warning(*msg):
    print(f"{__progname__}: ", *msg, file=sys.stderr, end="", flush=True, sep="")


def sf_error(*msg):
    print(f"{__progname__}: ", *msg, file=sys.stderr, end="", flush=True, sep="")
    sys.exit(1)


def _str_match_re(
    str_in: Optional[Union[str, list]],
    pattern: str = r'\s+(?=(?:[^"]*"[^"]*")*[^"]*$)',
    strip: Optional[str] = None
) -> dict:
    """
    Parse key=val pairs from input string/list.
    If '=' is present, everything after the first '=' is treated as val.
    If val is wrapped in matching quotes, remove them.
    """
    out_dict = {}
    if isinstance(str_in, str):
        tokens = re.split(pattern, str_in.strip(strip))
    else:
        tokens = str_in

    for token in tokens:
        if "=" in token:
            k, v = token.split("=", 1)
            k = k.strip()
            v = v.strip()
            if ((v.startswith('"') and v.endswith('"')) or
                (v.startswith("'") and v.endswith("'"))):
                v = v[1:-1]
            out_dict[k] = v
    return out_dict


def _is_valid_identifier(name: str) -> bool:
    return name.isidentifier() and not name.startswith("_")


def _inject_axis_vars(env: dict, arr, prefix: str = ""):
    """
    Inject axis-related variables into env.

    For prefix="":
        x1, x2, ... ; n1, n2, ... ; o1, o2, ... ; d1, d2, ...
    For prefix="b_":
        b_x1, b_x2, ... ; b_n1, ... etc.

    Here x1, x2, ... are coordinate vectors from arr.axis1, arr.axis2, ...
    """
    for i in range(1, 10):
        nkey = f"n{i}"
        okey = f"o{i}"
        dkey = f"d{i}"
        xkey = f"x{i}"

        val_n = None
        val_o = None
        val_d = None
        val_x = None

        try:
            if hasattr(arr, "shape") and len(arr.shape) >= i:
                val_n = int(arr.shape[i - 1])
        except Exception:
            pass

        try:
            val_x = getattr(arr, f"axis{i}")
        except Exception:
            val_x = None

        try:
            hdr = getattr(arr, "header", None)
            if hdr is not None:
                if okey in hdr:
                    val_o = float(hdr[okey])
                if dkey in hdr:
                    val_d = float(hdr[dkey])
        except Exception:
            pass

        if val_x is not None:
            try:
                if val_o is None and len(val_x) > 0:
                    val_o = float(val_x[0])
                if val_d is None and len(val_x) > 1:
                    val_d = float(val_x[1] - val_x[0])
            except Exception:
                pass

        if val_n is not None:
            env[f"{prefix}{nkey}"] = val_n
        if val_o is not None:
            env[f"{prefix}{okey}"] = val_o
        if val_d is not None:
            env[f"{prefix}{dkey}"] = val_d
        if val_x is not None:
            env[f"{prefix}{xkey}"] = val_x

def _collect_cmds(kargs: dict):
    """
    Collect commands in execution order:
        cmd, cmd0, cmd1, cmd2, ...
    Match:
        - cmd
        - cmd + arbitrary-length digits
    Sort rule:
        - cmd first
        - then cmd<number> by numeric order
    """
    cmds = []

    pattern = re.compile(r"^cmd(\d*)$")
    matched = []

    for key in list(kargs.keys()):
        m = pattern.match(key)
        if m is None:
            continue

        suffix = m.group(1)
        order = -1 if suffix == "" else int(suffix)
        matched.append((order, key, kargs.pop(key)))

    matched.sort(key=lambda x: (x[0], x[1]))

    for _, key, code in matched:
        cmds.append((key, code))

    return cmds


def main():
    no_filein = sys.stdin.isatty()
    kargs = _str_match_re(sys.argv[1:])

    if no_filein and len(sys.argv) < 2:
        subprocess.run(['less', '-R'], input=DOC.encode())
        sys.exit(1)

    cmds = _collect_cmds(kargs)
    outcmd = kargs.pop("output", None)

    binary_out = kargs.pop("--out", None)

    if len(cmds) == 0 and outcmd is None:
        print(cmds, outcmd, file=sys.stderr)
        sf_error("Need cmd/cmd0/cmd1/... or output=.\n")

    # -------- Reserved parameters for synthetic input when stdin is absent --------
    shape_in = []
    header = {}

    for idim in range(1, 10):
        nkey = f"n{idim}"
        okey = f"o{idim}"
        dkey = f"d{idim}"
        lkey = f"label{idim}"
        ukey = f"unit{idim}"

        nn = kargs.pop(nkey, None)
        if nn is not None:
            shape_in.append(int(nn))
        elif len(shape_in) > 0:
            shape_in.append(1)

        vo = kargs.pop(okey, None)
        vd = kargs.pop(dkey, None)
        vl = kargs.pop(lkey, None)
        vu = kargs.pop(ukey, None)

        if vo is not None:
            header[okey] = float(vo)
        if vd is not None:
            header[dkey] = float(vd)
        if vl is not None:
            header[lkey] = vl
        if vu is not None:
            header[ukey] = vu

    # -------- Build execution environment --------
    env = {
        "np": np,
        "Rsfarray": Rsfarray,
    }

    if not no_filein:
        try:
            input = Rsfarray(sys.stdin.buffer)
        except Exception as e:
            sf_error(f"Failed to read input RSF from stdin: {e}\n")
    else:
        if len(shape_in) == 0:
            sf_error("No input data and no shape specified. "
                     "Please provide either stdin RSF or n1=, n2=, ...\n")
        try:
            arr = np.zeros(shape_in, dtype=np.float32)
            input = Rsfarray(arr, header=header)
        except Exception as e:
            sf_error(f"Failed to create synthetic input array: {e}\n")

    env["input"] = input
    _inject_axis_vars(env, input, prefix="")

    # -------- Read other input files --------
    # remaining key=value pairs are interpreted as extra input RSF files
    for key, val in list(kargs.items()):
        if not _is_valid_identifier(key):
            sf_error(f"Invalid variable name: {key}\n")
        try:
            env[key] = Rsfarray(val)
            _inject_axis_vars(env, env[key], prefix=f"{key}_")
        except Exception as e:
            sf_error(f"Failed to load extra RSF file '{val}' as variable '{key}': {e}\n")

    # -------- Execute commands --------
    try:
        for key, code in cmds:
            exec(code, {}, env)
    except Exception as e:
        sf_error(f"Error while executing {key}: {e}\n")

    # -------- Evaluate output --------
    try:
        if outcmd is not None:
            output = eval(outcmd, {}, env)
        else:
            output = env.get("output", None)
    except Exception as e:
        sf_error(f"Error while evaluating output=: {e}\n")

    if output is None:
        sf_error("No output produced. "
                 "Please set output=... or assign variable `output` in cmd/cmd0/cmd1/...\n")

    # -------- Write output --------
    try:
        output.write(sys.stdout.buffer, out=binary_out)
    except Exception as e:
        sf_error(f"Failed to write output RSF: {e}\n")

    sys.exit(0)


if __name__ == "__main__":
    main()
