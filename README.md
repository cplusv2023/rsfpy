# RSFPY

RSFPY is a patch-style companion package for
[Madagascar](https://ahay.org) / `m8r` projects.  Its main job is to make
Madagascar plotting smoother: VPL figures, SVG figures, local viewing, and
remote SSH viewing all behave like one workflow.

Instead of replacing Madagascar, RSFPY patches `rsf.proj.Plot` and
`rsf.proj.Result` so existing `SConstruct` projects can keep using familiar
Madagascar commands while gaining better viewers and SVG/VPL handling.

## What It Adds

- `vplviewer`: view Madagascar VPL output directly.
- `svgviewer`: view SVG output, including RSFPY multi-frame SVG sequences.
- `rsfclient`: local GUI receiver for showing figures generated on a remote SSH server.
- `vpl2svg`: native VPL-to-SVG conversion used by the viewer tools.
- `rsfvpl2svg`: command-line VPL-to-SVG wrapper for saving converted SVG files.
- `rsfsvgpen`: SVG composition helper for overlaying, arranging, and combining figures.
- `rsfpy.m8r`: patch module that makes Madagascar `Plot`, `Result`, `svgPlot`, and `svgResult` prefer the RSFPY display path.

## Install

From this repository:

```bash
pip install .
```

For development:

```bash
pip install -e .
```

RSFPY builds and installs its native display tools as part of the normal package workflow.  If a native viewer dependency is missing, the Python package can still install, but that tool will be skipped unless `RSFPY_REQUIRE_NATIVE=1` is set.  The current default Python install includes `numpy` and `matplotlib`; `lxml` is only needed for `rsfsvgpen`.

### Dependency Groups

| Feature group | Python packages | Native build/runtime dependencies | Notes |
| --- | --- | --- | --- |
| Basic Python API | `numpy`, `matplotlib` in the current package install | C compiler for `rsfpy.plot.rsfpy_utils` | RSF read/write helpers and array utilities; plotting helpers are currently imported by `Rsfarray`. |
| Plotting commands | `numpy`, `matplotlib` | none beyond Python build basics | Provides `rsfgrey`, `rsfgrey3`, `rsfgraph`, and `rsfwiggle`. |
| Madagascar patch workflow | `numpy`, `matplotlib` | Madagascar / `m8r` available on `PATH` | Provides `rsfpy.m8r`, patched `Plot` / `Result`, and `svgPlot` / `svgResult`. |
| `svgviewer` X11 backend | same as above | X11, Cairo, librsvg, GLib | Legacy/lightweight SVG viewer backend. On macOS, XQuartz is needed to display X11 windows. |
| GTK-based `svgviewer` | same as above | GTK 4, Cairo, librsvg, GLib/GIO | Preferred local SVG viewer backend. macOS also builds an Objective-C clipboard helper using AppKit/Foundation. |
| `rsfclient` | same as above | GTK 4, GLib/GIO, OpenSSH client | GUI SSH reverse-tunnel receiver for remote display. |
| `vpl2svg` / `vplviewer` | same as above | C compiler, math library (`libm`) | `vpl2svg` itself is standalone C; `vplviewer` then hands converted SVG to `svgviewer`. |

Useful build switches:

```bash
RSFPY_BUILD_NATIVE=0 pip install -e .      # install Python pieces only
RSFPY_REQUIRE_NATIVE=1 pip install -e .    # fail if required native tools do not build
```

### macOS

Minimal Python + plotting + `vpl2svg` build tools:

```bash
xcode-select --install
brew install pkgconf
pip install -e .
```

Basic plotting plus X11 `svgviewer`:

```bash
brew install pkgconf cairo glib librsvg libx11
pip install -e .
```

GTK `svgviewer` and `rsfclient`:

```bash
brew install pkgconf gtk4 librsvg cairo glib libx11
pip install -e .
```

For the X11 backend, install and start XQuartz separately, then make sure `DISPLAY` is valid.

On Apple Silicon, make sure Homebrew's `pkg-config` files are visible:

```bash
export PATH="/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig:$PKG_CONFIG_PATH"
```

### Ubuntu / Debian

Minimal Python + plotting + `vpl2svg` build tools:

```bash
sudo apt update
sudo apt install build-essential pkg-config
pip install -e .
```

Basic plotting plus X11 `svgviewer`:

```bash
sudo apt update
sudo apt install build-essential pkg-config \
    libx11-dev libcairo2-dev librsvg2-dev libglib2.0-dev

pip install -e .
```

GTK `svgviewer` and `rsfclient`:

```bash
sudo apt update
sudo apt install build-essential pkg-config \
    libgtk-4-dev librsvg2-dev libcairo2-dev libglib2.0-dev libx11-dev

pip install -e .
```

If you only need GTK and not the X11 fallback, `libx11-dev` can be omitted.

### Windows

Use the packaged RSFPY viewer/client bundle for normal Windows viewing.  Keep the extracted directory intact and run:

```cmd
rsfclient.exe
```

The bundled `rsfclient.exe`, `svgviewer.exe`, DLLs, `lib/`, `share/`, and `etc/` directories are one runtime package.

## Patch Madagascar Projects

In an `SConstruct`, import RSFPY's m8r patch before using `Plot` or `Result`:

```python
from rsf.proj import *
from rsfpy.m8r import *
```

Then write normal Madagascar plotting rules:

```python
Flow('spike', None, 'spike n1=1001 d1=0.004 o1=-2 k1=500')
Plot('graph', 'spike', 'bandpass flo=5 fhi=6 | window min1=-0.5 max1=0.5 | graph')
Result('graph', 'spike', 'bandpass flo=5 fhi=6 | window min1=-0.5 max1=0.5 | graph')
End()
```

With the patch loaded, VPL output is viewed through `vplviewer` when available.  SVG output is viewed through `svgviewer`.

For direct SVG products:

```python
svgPlot('fig_svg', 'spike', 'graph | rsfvpl2svg')
svgResult('fig_svg', 'spike', 'graph | rsfvpl2svg')
```

## RSFPY Plotting Commands

RSFPY also provides SVG-oriented plotting commands that mirror common Madagascar plotting habits while producing SVG directly through Matplotlib-based renderers.

| Command | Use it for |
| --- | --- |
| `rsfgrey` | 2-D grey/color image plots for regularly sampled RSF data. |
| `rsfgraph` | 1-D curves and trace plots. |
| `rsfwiggle` | Seismic-style wiggle plots. |
| `rsfgrey3` | 3-D cube-style views for volume data. |

These commands are useful when you want SVG output directly:

```bash
sfspike n1=1001 d1=4e-3 o1=-2 k1=500 |
sfbandpass flo=5 fhi=6 |
sfwindow min1=-0.5 max1=0.5 |
rsfgraph > graph.svg

svgviewer graph.svg
```

### rsfgrey

`rsfgrey` draws 2-D sampled data as a grey or color image:

```bash
sfspike n1=128 n2=128 k1=64 k2=64 |
rsfgrey title="Impulse" color=j > grey.svg
```

It is the direct-SVG counterpart for image-style RSF plots, with familiar parameters such as labels, titles, color maps, clipping, bias, and frame styling.

### rsfgraph

`rsfgraph` draws 1-D traces and curves:

```bash
sfspike n1=1001 d1=4e-3 o1=-2 k1=500 |
sfbandpass flo=5 fhi=6 |
sfwindow min1=-0.5 max1=0.5 |
rsfgraph title="Bandpassed spike" label1="Time" unit1=s > graph.svg
```

Use it for line plots, trace comparison, and quick inspection of sampled signals.

### rsfwiggle

`rsfwiggle` draws wiggle plots for seismic-style sections:

```bash
sfspike n1=256 n2=32 k1=80 |
rsfwiggle title="Wiggle" transp=y > wiggle.svg
```

It is useful when polarity, trace shape, and lateral trace spacing matter more than a raster image.

### rsfgrey3

`rsfgrey3` draws cube-style views for 3-D data:

```bash
sfspike n1=64 n2=64 n3=32 k1=32 k2=32 k3=16 |
rsfgrey3 title="Cube" frame1=32 frame2=32 frame3=16 > grey3.svg
```

Use it for quick 3-D volume inspection.  For Madagascar VPL cube output, use `vplviewer` or `rsfvpl2svg`; for direct SVG cube output, use `rsfgrey3`.

The SVG outputs from these commands can be opened directly:

```bash
svgviewer grey.svg graph.svg wiggle.svg grey3.svg
```

or composed with `rsfsvgpen`:

```bash
rsfsvgpen grey.svg graph.svg mode=grid ncol=1 > panel.svg
svgviewer panel.svg
```

## View VPL Output

Pipe VPL from Madagascar into `vplviewer`:

```bash
sfspike n1=1001 d1=4e-3 o1=-2 k1=500 |
sfbandpass flo=5 fhi=6 |
sfwindow min1=-0.5 max1=0.5 |
sfgraph |
vplviewer
```

Open VPL files:

```bash
vplviewer graph.vpl
vplviewer grey.vpl grey3.vpl
```

Set conversion options with Madagascar-style `key=value` arguments:

```bash
vplviewer graph.vpl bgcolor=w font=Helvetica fontsz=14 axiscolor=k
vplviewer grey3.vpl bgcolor=k fontcolor=w axiscolor=#ff5555 axisfat=3
```

Defaults can be supplied through `RSFVPLOPTS`:

```bash
export RSFVPLOPTS='bgcolor=w font=Helvetica fontsz=14'
vplviewer graph.vpl
```

Useful VPL/SVG conversion options include:

- `bgcolor=white`, `bgcolor=k`, `bgcolor=#ffffff`
- `font=Helvetica`, `fontfamily="Times New Roman"`
- `fontsz=14`, `fontcolor=k`, `fontfat=2`
- `labelsz=12`, `labelcolor=#333333`
- `titlecolor=red`, `titlesz=16`
- `framecolor=k`, `framefat=2`
- `axiscolor=k`, `axisfat=2`
- `gridcolor=#cccccc`, `gridfat=1`

## Convert VPL To SVG

`rsfvpl2svg` is the save-to-file version of the VPL viewer conversion path.

Convert one file:

```bash
rsfvpl2svg graph.vpl
```

This writes:

```text
graph.svg
```

Choose an output path:

```bash
rsfvpl2svg graph.vpl graph.svg
```

Convert multiple files:

```bash
rsfvpl2svg graph.vpl grey.vpl grey3.vpl
```

Use conversion options:

```bash
rsfvpl2svg grey3.vpl bgcolor=w font=Helvetica axisfat=2
```

Read from stdin and write SVG to stdout:

```bash
sfspike n1=100 | sfgraph | rsfvpl2svg > graph.svg
```

If stdout is a terminal, RSFPY refuses to dump raw SVG text and prints:

```text
You don't want to dump plain svg-text to terminal.
```

### Standard SVG Output

By default, RSFPY preserves its multi-frame SVG sequence markers:

```bash
rsfvpl2svg grey_10frames.vpl
```

Use `standard=y` to write ordinary SVG files:

```bash
rsfvpl2svg grey_10frames.vpl standard=y
```

For multi-frame VPL input, this writes:

```text
grey_10frames_1.svg
grey_10frames_2.svg
...
grey_10frames_10.svg
```

For stdin with `standard=y`, only the first frame is written to stdout:

```bash
sfspike n1=100 | sfgraph | rsfvpl2svg standard=y > first.svg
```

### Concatenate Frames

`cat=y` has priority over `standard=y`.  It converts every input frame and writes a single RSFPY multi-frame SVG sequence to stdout:

```bash
rsfvpl2svg graph.vpl grey_10frames.vpl cat=y > sequence.svg
svgviewer sequence.svg
```

This is a frame sequence, not a single large drawing canvas.

## View SVG Output

Open SVG files:

```bash
svgviewer graph.svg
svgviewer graph.svg grey.svg
```

Pipe SVG:

```bash
sfspike n1=100 | sfgraph | rsfvpl2svg | svgviewer
```

Force a backend:

```bash
svgviewer backend=gtk graph.svg
svgviewer --backend gtk graph.svg
svgviewer --backend x11 graph.svg
```

The wrappers use Madagascar-style argument parsing: `key=value` values are options, and non-`key=value` values are input files.

## Compose SVG Figures

`rsfsvgpen` combines SVG figures after they have been generated.

Overlay:

```bash
rsfsvgpen graph.svg wiggle.svg mode=overlay bgcolor=w > overlay.svg
```

Grid:

```bash
rsfsvgpen graph.svg grey.svg mode=grid ncol=1 > stacked.svg
rsfsvgpen graph.svg grey.svg mode=grid nrow=1 > side_by_side.svg
```

View the result:

```bash
svgviewer stacked.svg
```

## Remote Display With rsfclient

Run `rsfclient` on the local machine:

```bash
rsfclient
```

Create or select an SSH profile and connect.  RSFPY sets up a paired reverse tunnel and writes a small pairing file on the remote side when possible.  Remote commands can then send SVG or VPL-derived SVG back to the local viewer.

From the remote shell:

```bash
sfspike n1=100 | sfgraph | svgviewer
```

or:

```bash
sfspike n1=100 | sfgraph | rsfvpl2svg | svgviewer
```

The remote `svgviewer` first tries the paired `rsfclient` receiver.  If no pairing information is available, it falls back to the configured environment and viewer backend rules.

Useful environment variables:

```bash
export RSFPY_SVGVIEWER_BACKEND=auto
export RSFVPLOPTS='bgcolor=w font=Helvetica'
```

`rsfclient` is most useful when:

- the compute machine has no desktop session;
- X11 forwarding is slow or unavailable;
- Windows users want a local receiver instead of an X server;
- temporary plots should appear locally without manually copying files.

## Commands

| Command | Purpose |
| --- | --- |
| `vplviewer` | Convert VPL to SVG and show it. |
| `rsfvpl2svg` | Convert VPL to SVG files or SVG frame sequences. |
| `svgviewer` | Show SVG files, stdin SVG, or RSFPY SVG frame sequences. |
| `rsfclient` | Local GUI receiver and SSH tunnel manager for remote display. |
| `rsfsvgpen` | Compose/overlay/grid SVG figures. |
| `rsfgrey`, `rsfgraph`, `rsfwiggle`, `rsfgrey3` | SVG-oriented plotting commands provided by RSFPY. |

## Version

Current version: `1.0.0`

## License

RSFPY is distributed under the GPL-2.0-only license.
