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
