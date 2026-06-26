# -*- coding: utf-8 -*-
"""Parameter aliases shared by RSFPY plotting commands."""


PARAMETER_ALIASES = {
    "screenwidth": ("width", "figwidth", "figurewidth"),
    "screenheight": ("height", "figheight", "figureheight"),
    "color": ("cmap", "colormap"),
    "bgcolor": ("facecolor",),
    "scalebar": ("colorbar",),
    "barlabel": ("bartitle", "bar_title"),
    "barlabelsz": ("barlabelsize",),
    "barlabelfat": ("barlabelweight",),
    "formatbar": ("barformat",),
    "font": ("fontfamily", "family"),
    "fontsz": ("fontsize",),
    "fontfat": ("fontweight",),
    "labelfat": ("labelweight",),
    "labelsz": ("labelsize",),
    "titlefat": ("titleweight",),
    "titlesz": ("titlesize",),
    "tickfat": ("tickweight",),
    "ticksz": ("ticksize",),
    "framewidth": ("framefat", "framelinewidth"),
    "axiswidth": ("axisfat", "axislinewidth"),
    "gridwidth": ("gridfat", "gridlinewidth"),
    "plotfat": ("linewidth", "linefat", "plotwidth"),
    "maxpixels": ("max_pixels",),
    "lcolor": ("plotcol", "linecolor"),
    "lstyles": ("linestyles",),
    "markers": ("symbols",),
    "markersize": ("markersz", "symbolsize", "symbolsz"),
    "legendfat": ("legendweight",),
    "xlabel": ("label2",),
    "ylabel": ("label1",),
    "zlabel": ("label3",),
    "ntic1": ("ntick1",),
    "ntic2": ("ntick2",),
    "ntic3": ("ntick3",),
    "nticbar": ("ntickbar",),
}


def canonical_params(params):
    """Return a copy with canonical keys filled from aliases.

    Original keys are preserved so old Madagascar-style calls keep working.
    Canonical keys only fill missing values; explicitly provided canonical
    values always win over aliases.
    """

    out = dict(params)
    for canonical, aliases in PARAMETER_ALIASES.items():
        if canonical in out and out[canonical] not in (None, ""):
            continue
        for alias in aliases:
            if alias in out and out[alias] not in (None, ""):
                out[canonical] = out[alias]
                break
    return out
