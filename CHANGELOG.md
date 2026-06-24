# Changelog

## Version 0.1.0 - 2025-09-18
### Added
- Initial release. Everything seems fine.

## Version 0.1.1 - 2025-09-20
### Added
- Movie mode in **rsfgrey** and **rsfsvgpen**.
- Several parameter aliases (I always forget their original names).

### Changed
- Updated the UI of **svgviewer**: replaced text buttons with icon buttons.

### Fixed
- Fixed numerous bugs (details not recorded at the time).

## Version 0.1.2 - 2025-10-12
Finally decided to add this changelog.
### Added
- **This changelog**.
- Support for uchar data (int8) in **rsfgrey** and **rsfgrey3**.  
  You can use **sfbyte** (from Madagascar) to preprocess data before drawing images.  
  Remember to generate a colormap file using **sfbyte** if you want to display a colormap when working with uchar data.
- Movie mode for **rsfgrey3**.
- Alignment settings for **rsfsvgpen** in grid mode. Supports *ha=* and *va=*.
- Stem mode for **rsfgraph** (try it with *stem=y mark=o*).
- Ability to edit font-size (scaling factor), font-family, and font-weight in **rsfsvgpen**.

### Changed
- Optimized SVG rendering performance in **svgviewer**.
- Text elements **will no longer** be converted to paths when saving figures as SVG in **rsfgraph**, **rsfgrey**, **rsfwiggle**, and **rsfgrey3**.
- Text elements **will no longer** be stretched when using *stretchx=y* or *stretchy=y* in **rsfsvgpen**.
- Font-weight parameters are now **always** normalized to one of the standard keywords (e.g., `"normal"`) in **rsfgraph**, **rsfgrey**, **rsfwiggle**, and **rsfgrey3**.

### Fixed
- Improved handling of oversized SVG files in **rsfsvgpen** (lxml).
- Corrected key bindings in **svgviewer** (N=next, M=prev).
- Fixed missing error messages in **svgviewer**.
- Fixed errors when loading files from stdin in **svgviewer**.
- Corrected pixel units in **rsfsvgpen** (now consistently uses px).
- Updated self-documentation.

### Known Issues
- Difficulty reading oversized SVG files with **svgviewer** (librsvg2).
- Low efficiency in movie mode for **rsfgrey** and **rsfgrey3**. Use *maxframe=* to control the maximum number of frames.
- Stretched subfigures in **svgviewer** may sometimes look odd. Try adjusting the figure height/width parameters when drawing.

## Version 0.1.3 - 2025-10-21
### Added
- Support for TeX font rendering in text labels for **rsfgraph**, **rsfgrey**, **rsfwiggle**, and **rsfgrey3**.
- C API integration to accelerate **rsfwiggle** plot rendering.



### Fixed
- Improved SCons compatibility for **m8r**; resolved potential issues with the *PATH* environment variable.
- Corrected rsfgraph rendering bug where curves always appeared black instead of following the color cycle.
- Fixed SVG parsing failure in **svgviewer** when encountering oversized base64-encoded images (librsvg2-related).

### Knwon Issues
- Low efficiency in movie mode for **rsfgrey** and **rsfgrey3**. Use *maxframe=* to control the maximum number of frames.
- Stretched subfigures in **svgviewer** may sometimes look odd. Try adjusting the figure height/width parameters when drawing.
- Reduced *X11* rendering performance in **svgviewer** during image dragging and zooming, especially over *SSH tunnels*.



## Version 0.1.4 - 2026-06-18

### Added

* Added a new GTK backend for **svgviewer**.
* Added annotation tools in the GTK viewer, including brush, dashed brush, rectangle, dashed rectangle, arrow, eraser, color selection, line-width selection, clear, undo, and redo.
* Added box-zoom interaction in the GTK viewer.
* Added optional GTK/X11 backend selection for the SVG viewer. The GTK backend is preferred when available, while the X11 backend remains supported as a fallback.
* Added support for building both `svgviewer-gtk` and `svgviewer-x11`, with automatic fallback when one backend is unavailable.

### Changed

* Refactored the GTK viewer UI to use native GTK controls for the toolbar, pen toolbar, and status bar.
* Improved the SVG viewer rendering architecture by separating the viewer frontend from the SVG sequence layer.
* Improved annotation interaction: drawing now starts on mouse press and finishes on mouse release.
* Moved undo/redo controls into the pen toolbar.
* Updated the SVG viewer launcher to prefer the GTK backend and fall back to X11 when needed.

### Fixed

* Improved SVG viewer responsiveness during drawing and annotation operations.
* Improved handling of large or complex SVG sequences by reducing unnecessary rendering and UI coupling.
* Fixed several UI and interaction issues in the new GTK viewer.

### Known Issues

* The GTK backend is still experimental and may behave differently from the legacy X11 viewer.
* The X11 backend is still supported, but may be deprecated in a future release.
* Very large SVG files containing oversized embedded Base64 bitmap data may still fail to load due to librsvg safety limits.
* Some annotation and zoom interactions may still require further refinement.

## Version 1.0.0 - 2026-06-24

### Added

* Added **RSFPY m8r patch** support as a first-class workflow. Importing `rsfpy.m8r` patches Madagascar `rsf.proj.Plot` and `rsf.proj.Result` so VPL output prefers `vplviewer`, while SVG output prefers `svgviewer`.
* Added `svgPlot` and `svgResult` helpers for SVG-oriented Madagascar project rules.
* Added **vplviewer**, a VPL display runner that converts Madagascar VPL streams/files to SVG and then opens them through the RSFPY SVG viewer path.
* Added **vpl2svg**, a standalone native C VPL-to-SVG converter independent of Madagascar libraries.
* Added **rsfvpl2svg**, a command-line wrapper for saving VPL conversions as SVG:
  * default output preserves RSFPY multi-frame SVG sequence markers;
  * `standard=y` writes ordinary SVG files and splits multi-frame input into numbered files;
  * `cat=y` concatenates all input frames into one RSFPY multi-frame SVG sequence on stdout.
* Added `RSFVPLOPTS` for persistent VPL conversion defaults such as `bgcolor=`, `font=`, `fontsz=`, `axiscolor=`, and `gridfat=`.
* Added VPL text, raster image, multi-frame stream, color table, axis/grid/frame styling, and basic Greek font switching support through `\F9`.
* Added native handling for common Madagascar VPL output from `grey`, `grey3`, `graph`, `wiggle`, and multi-frame plotting flows.
* Added grey3 cube/corner support in the new VPL converter, including handling of Madagascar's `corner` group and `VP_BLACK` cleanup semantics.
* Added a profile-based **rsfclient** GUI for remote display through SSH reverse tunnels.
* Added paired remote sender configuration for `rsfclient`, including random port selection and token-based send validation.
* Added Windows packaging support for the viewer/client bundle, including `svgviewer`, `rsfclient`, GTK runtime resources, GLib/GIO helpers, and dependency manifests.

### Changed

* Repositioned RSFPY as a Madagascar / `m8r` patch-style plotting and display package rather than a general Python array API package.
* Rewrote `README.md` around the patched `Plot` / `Result` workflow, VPL/SVG viewing, VPL-to-SVG conversion, SVG composition, and remote display.
* Updated project version to **1.0.0**.
* Changed viewer wrappers to use Madagascar-style argument parsing:
  * non-`key=value` arguments are treated as input files;
  * `key=value` arguments are treated as options;
  * unknown options are ignored;
  * `--backend` remains supported as a special viewer option.
* Changed `font=` to act as an alias for `fontfamily=`.
* Changed `svgviewer` to read invalid or inaccessible non-key arguments permissively instead of raising immediately.
* Changed `Result(..., suffix='.vpl')` viewing behavior to use the new `vplviewer` display path when available.
* Improved `svgviewer` GTK UI with a more compact toolbar, icon-based annotation controls, export support, context menu actions, smaller minimum window size, and better stretch/zoom behavior.
* Improved `rsfclient` profile editing by simplifying basic fields and moving less common SSH/display settings into advanced controls.
* Improved remote send behavior so `svgviewer` can use `rsfclient` pairing information before falling back to other viewer behavior.

### Fixed

* Fixed bad SVG/VPL input handling so viewer wrappers can show or report conversion errors more consistently.
* Fixed `rsfclient` send status so tunnel success is not reported before SSH authentication/forwarding is actually usable.
* Fixed password/askpass cancellation flow so cancelling authentication can disconnect the client workflow.
* Fixed `svgviewer` exit handling to avoid falling through into the wrong backend after normal quit.
* Fixed several clipboard/export edge cases in the GTK SVG viewer, with platform-specific SVG/PNG clipboard behavior improved especially on Windows and macOS.
* Fixed Windows packaging issues around helper executables, GdkPixbuf loaders, GIO modules, and missing runtime DLLs such as `vulkan1.dll`.

### Notes

* `rsfmath` and the lower-level Python/NumPy API remain in the package, but the 1.0.0 user-facing documentation now focuses on Madagascar plotting, VPL/SVG conversion, viewing, and remote display.
* `cat=y` in `rsfvpl2svg` creates a frame sequence, not one large combined drawing canvas.
