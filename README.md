# rsfpy

**rsfpy** is a Python toolkit for reading, writing, manipulating, and plotting [Madagascar](https://ahay.org) [RSF (Regularly Sampled Format)](https://ahay.org/wiki/Guide_to_RSF_file_format) scientific datasets.

Built on top of [NumPy](https://numpy.org), it supports efficient slicing, transposing, windowing, and subsampling, while automatically updating RSF metadata such as `n#`, `o#`, `d#`, `label#`, and `unit#` so that axis descriptions remain consistent after data transformations.

## ✨ Features

* 📂 **RSF data I/O**: read and write RSF header/data files.
* 🧮 **Array-like manipulation**: slice, transpose, window, and subsample RSF datasets with metadata synchronized automatically.
* 📈 **Command-line plotting**: generate SVG figures using tools such as `rsfgrey`, `rsfgraph`, `rsfwiggle`, and `rsfgrey3`.
* 🖊 **SVG composition**: combine, overlay, and arrange SVG figures using `rsfsvgpen`.
* 🖥 **Lightweight SVG viewing**: view generated SVG figures locally with `svgviewer`.
* 🔁 **Remote display workflow**: send SVG figures from an SSH server back to your local machine using `rsfclient`.
* 🌍 **Applications**: geophysics, signal processing, seismic imaging, and scientific visualization.
* 🤝 **Community driven**: the project is still growing, and contributions are welcome.

## 📦 Installation

It is strongly recommended to use a virtual Python environment to isolate dependencies and ensure reproducibility.

```bash
pip install .
```

Install Python requirements by:

```bash
pip install -r requirements.txt
```

## 📚 Requirements

### Python requirements

* Python >= 3
* NumPy
* Matplotlib
* Optional: `lxml`, required only for `rsfsvgpen`

### Optional native tools

rsfpy includes optional native tools for SVG viewing and remote display.

| Tool            | Purpose                                                              | Required?                             |
| --------------- | -------------------------------------------------------------------- | ------------------------------------- |
| `svgviewer`     | Open SVG figures locally                                             | Optional, but recommended             |
| `svgviewer-gtk` | GTK4 backend for SVG viewing                                         | Optional native backend               |
| `svgviewer-x11` | X11 fallback backend for SVG viewing                                 | Optional native backend               |
| `rsfclient`     | Receive SVG figures from remote SSH servers and display them locally | Optional, useful for remote workflows |

At least one SVG viewer backend should be available if you want to use `svgviewer`.

## 🖥 Why is there an `rsfclient`?

Many rsfpy workflows are run on a remote Linux server, workstation, or HPC node through SSH. In that situation, plotting commands such as:

```bash
rsfgrey < data.rsf > figure.svg
svgviewer figure.svg
```

may generate the SVG file successfully, but displaying it can be inconvenient because:

* the remote server may not have a graphical desktop;
* X11 forwarding can be slow or unavailable;
* Windows users may not have an X server installed;
* generated figures are often temporary and only need to be viewed locally;
* repeatedly copying SVG files from the server to the local computer interrupts the workflow.

`rsfclient` solves this by running a small receiver on your local machine. It opens an SSH reverse tunnel so that the remote server can send SVG data back to your local computer.

The workflow is:

```text
Remote server
    rsfgrey / rsfgraph / rsfwiggle
        |
        v
    SVG data
        |
        v
    rsfclient --send
        |
        v
SSH reverse tunnel
        |
        v
Local machine
    rsfclient receiver
        |
        v
    svgviewer
```

In short:

* `svgviewer` is the local SVG viewer.
* `rsfclient` is the local receiver for remote SVG display.
* `rsfclient --send` is the remote-side sender.
* Local-only users do not need `rsfclient`.

A typical remote usage example is:

```bash
sfspike n1=100 | rsfgraph | rsfclient --send
```

If you use a custom port:

```bash
sfspike n1=100 | rsfgraph | rsfclient --send --port 17891
```

For shared servers, each user should use a different remote port, for example:

```bash
export RSFVIEW_PORT=17891
```

and then send figures by:

```bash
sfspike n1=100 | rsfgraph | rsfclient --send --port "$RSFVIEW_PORT"
```

If a transfer token is enabled in the local client, set:

```bash
export RSFVIEW_TOKEN=your_token
```

Then:

```bash
sfspike n1=100 | rsfgraph | rsfclient --send --port "$RSFVIEW_PORT"
```

## 🧩 Native dependencies for SVG viewer/client

The SVG viewer has two optional native backends:

* `svgviewer-gtk`: GTK4 backend, preferred when available;
* `svgviewer-x11`: X11 backend, fallback backend.

The remote display client `rsfclient` uses GTK for its local GUI.

### Ubuntu / Debian

For the GTK4 backend and client:

```bash
sudo apt update
sudo apt install build-essential pkg-config \
    libgtk-4-dev librsvg2-dev libcairo2-dev libglib2.0-dev
```

For the X11 fallback backend:

```bash
sudo apt install build-essential pkg-config \
    libx11-dev libcairo2-dev librsvg2-dev libglib2.0-dev
```

To support both GTK4 and X11:

```bash
sudo apt install build-essential pkg-config \
    libgtk-4-dev libx11-dev libcairo2-dev librsvg2-dev libglib2.0-dev
```

### macOS / Homebrew

For the GTK4 backend and client:

```bash
brew install pkgconf gtk4 librsvg cairo glib
```

For the X11 fallback backend:

```bash
brew install pkgconf libx11 cairo librsvg glib
```

On Apple Silicon Macs, if `pkg-config` cannot find Homebrew packages, set:

```bash
export PATH="/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig:$PKG_CONFIG_PATH"
```

Then verify:

```bash
pkg-config --exists gtk4 && echo "gtk4 ok"
pkg-config --exists librsvg-2.0 && echo "librsvg ok"
```

### Windows

For normal Python usage, install rsfpy as usual.

For the standalone viewer/client, download the Windows viewer client package from the project release page, for example:

```text
rsfpy-viewer-client-win64-*.zip
```

Extract the whole directory and run:

```cmd
rsfclient.bat
```

Do not move individual `.exe` files out of the extracted directory, because the GTK runtime files, DLLs, `lib/`, `share/`, and `etc/` directories are required.

## 🚀 Quick start

### Command-line plotting tools

Two main plotting components are provided:

* `rsfgrey`, including variants such as `rsfgraph`, `rsfwiggle`, and `rsfgrey3`;
* `rsfsvgpen`, used to concatenate, merge, overlay, or arrange SVG figures.

Each command-line tool is self-documented. Run the command without arguments to view its usage.

```bash
rsfgrey < test/dat.test > test1.svg
rsfwiggle < test/dat.test pcolor=r ncolor=b transp=y zplot=2 > test2.svg

rsfsvgpen < test1.svg test2.svg mode=overlay bgcolor=w > overlay.svg
rsfsvgpen < test1.svg test2.svg mode=grid ncol=1 > rows.svg
rsfsvgpen < test1.svg test2.svg mode=grid nrow=1 > cols.svg

svgviewer < rows.svg cols.svg
```

You can also use other SVG viewers, such as a web browser or Inkscape. `svgviewer` is provided only for convenience and for smoother rsfpy workflows.

![svgviewer-wavefield snapshot](https://raw.githubusercontent.com/cplusv2023/something/046a5760218bfccde34c9e0231e776eaa9be1e2c/sources/wfl_demo.gif)

![svgviewer](./img/cols.png)

## 🐍 Python API

### Import

```python
from rsfpy.array import Rsfarray
```

### Read RSF data

Read from an RSF file path:

```python
rarray = Rsfarray("./dat.test")
```

Read from a file object:

```python
with open("./test/dat.test") as fp:
    rarray = Rsfarray(fp)
```

### Initialize from NumPy ndarray

```python
import numpy as np
from rsfpy.array import Rsfarray

narray = np.array([1, 2, 3])

rarray = Rsfarray(
    narray,
    header={
        "d1": 1,
        "o1": 0,
        "label1": "X",
        "unit1": "",
    },
    history="Ndarray [1, 2, 3]",
)
```

### Empty array and override

```python
empty = Rsfarray()
empty.read("./test/dat.test")
```

### Write RSF data

Write to an RSF file:

```python
rarray.write(
    "./test/saved.test",  # header file
    out="stdin",          # data path; "stdin" or None means append to header file
    form="ascii",         # use "native" for binary
    fmt="%.4e",           # ASCII data format, default is "%f"
)
```

Write to file-like objects:

```python
import io

meta = io.StringIO()
dat = io.BytesIO()

rarray.write(meta, out=dat)
```

### Use Rsfarray properties

```python
data = Rsfarray("./test/dat.test")

# Axis objects
taxis, xaxis = data.axis1, data.axis2
taxis, xaxis = data.axis([0, 1])

# Sampling parameters
nt, dt, ot = data.n1, data.d1, data.o1
nx, dx, ox = data.n2, data.d2, data.o2

label1, unit1 = data.label1, data.unit1
label2, unit2 = data.label2, data.unit2

# Or use compact accessors
nt, nx = data.n([0, 1])
dt, dx = data.d([0, 1])
```

### Transpose while preserving metadata

```python
print("before transpose:", data.label1, data.label2)

data_t = data.T

print("after transpose:", data_t.label1, data_t.label2)
```

### Window data while preserving metadata

```python
print("before window:", data.d1, data.o1)

data1 = data.window("j1=5 f1=100")
# or
data1 = data.window(j1=5, f1=100)

print("after window:", data1.d1, data1.o1)
```

### Plotting with Matplotlib

```python
import matplotlib.pyplot as plt
from rsfpy.array import Rsfarray

data = Rsfarray("./test/dat.test")

fig, ax = plt.subplots(2, 1, figsize=(6, 8))

data.grey(
    cmap="seismic",
    show=False,
    ax=ax[0],
    colorbar=True,
    title="Grey plot",
)

data.wiggle(
    transp=True,
    yreverse=True,
    show=False,
    ax=ax[1],
    colorbar=True,
    title="Wiggle plot",
    zplot=2.0,
)

fig.set_tight_layout(True)
plt.show()
```

![Grey/wiggle image plot](./img/figure1.png)

## 🔁 Remote display examples

Start the local client on your computer:

```bash
rsfclient
```

or on Windows:

```cmd
rsfclient.bat
```

Configure an SSH profile, connect, and then run plotting commands on the remote server:

```bash
sfspike n1=100 | rsfgraph | rsfclient --send
```

If multiple users share the same server, use a unique port:

```bash
sfspike n1=100 | rsfgraph | rsfclient --send --port 17891
```

If a token is required:

```bash
RSFVIEW_TOKEN=your_token sfspike n1=100 | rsfgraph | rsfclient --send --port 17891
```

## 📝 Changelog

View the changelog [here](./CHANGELOG.md).

## 📄 License

GNU GPLv2

## 🤝 Contributing

Contributions are welcome. Feel free to open an issue or submit a pull request to improve **rsfpy**.
