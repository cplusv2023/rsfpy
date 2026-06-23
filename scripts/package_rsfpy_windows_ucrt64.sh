#!/usr/bin/env bash
set -euo pipefail

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    echo "ERROR: run this from the MSYS2 UCRT64 shell. MSYSTEM=${MSYSTEM:-<empty>}" >&2
    exit 1
fi

VERSION="${VERSION:-0.1.4}"
CC="${CC:-cc}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="${OUTDIR:-$ROOT/dist/rsfpy-viewer-win64-$VERSION}"
BUILDDIR="${BUILDDIR:-$ROOT/build-win64}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/src/rsfpy/tools}"
UCRT="${UCRT:-/ucrt64}"
BUILD_X11="${BUILD_X11:-0}"
COPY_GTK_MODULES="${COPY_GTK_MODULES:-0}"

SVGVIEWER_C="${SVGVIEWER_C:-$TOOLS_DIR/svgviewer.c}"
SVGSEQUENCE_C="${SVGSEQUENCE_C:-$TOOLS_DIR/svgsequence.c}"
SVGVIEWER_X11_C="${SVGVIEWER_X11_C:-$TOOLS_DIR/svgviewer_x11.c}"
SVGSEQUENCE_X11_C="${SVGSEQUENCE_X11_C:-$TOOLS_DIR/svgsequence_x11.c}"
RSFCLIENT_C="${RSFCLIENT_C:-$TOOLS_DIR/rsfclient.c}"

ICON_VIEWER="${ICON_VIEWER:-}"
ICON_CLIENT="${ICON_CLIENT:-}"

log() {
    printf '==> %s\n' "$*"
}

require_cmd() {
    command -v "$1" >/dev/null || {
        echo "ERROR: required command not found: $1" >&2
        exit 1
    }
}

pkg_flags() {
    pkg-config --cflags --libs "$@"
}

build_exe() {
    local out="$1"
    shift
    local -a srcs=()
    while [[ "$#" -gt 0 && "$1" != "--" ]]; do
        srcs+=("$1")
        shift
    done
    shift
    local -a pkgs=("$@")

    for src in "${srcs[@]}"; do
        [[ -f "$src" ]] || {
            echo "ERROR: source not found: $src" >&2
            exit 1
        }
    done

    "$CC" -std=gnu99 -O2 "${srcs[@]}" \
        -o "$out" \
        $(pkg_flags "${pkgs[@]}") \
        -mwindows
}

copy_if_exists() {
    local src="$1"
    local dst="$2"
    if [[ -e "$src" ]]; then
        mkdir -p "$(dirname "$dst")"
        cp -a "$src" "$dst"
    fi
}

log "Cleaning output directory"
rm -rf "$OUTDIR"
mkdir -p "$BUILDDIR" "$OUTDIR"

log "Checking tools"
require_cmd "$CC"
require_cmd pkg-config
require_cmd ldd

log "Checking pkg-config packages"
pkg-config --modversion gtk4 >/dev/null
pkg-config --modversion librsvg-2.0 >/dev/null
pkg-config --modversion cairo >/dev/null
pkg-config --modversion gio-2.0 >/dev/null
pkg-config --modversion glib-2.0 >/dev/null
pkg-config --modversion gdk-pixbuf-2.0 >/dev/null

log "Building svgviewer-gtk.exe"
viewer_sources=("$SVGVIEWER_C" "$SVGSEQUENCE_C")
[[ -n "$ICON_VIEWER" && -f "$ICON_VIEWER" ]] && viewer_sources+=("$ICON_VIEWER")
build_exe "$OUTDIR/svgviewer-gtk.exe" "${viewer_sources[@]}" -- \
    gtk4 librsvg-2.0 cairo glib-2.0 gio-2.0 gdk-pixbuf-2.0
cp -f "$OUTDIR/svgviewer-gtk.exe" "$OUTDIR/svgviewer.exe"

if [[ "$BUILD_X11" == "1" ]]; then
    log "Building svgviewer-x11.exe"
    pkg-config --modversion x11 >/dev/null
    build_exe "$OUTDIR/svgviewer-x11.exe" "$SVGVIEWER_X11_C" "$SVGSEQUENCE_X11_C" -- \
        x11 cairo glib-2.0 librsvg-2.0 gdk-pixbuf-2.0
else
    log "Skipping svgviewer-x11.exe (set BUILD_X11=1 to include it)"
fi

if [[ -f "$RSFCLIENT_C" ]]; then
    log "Building rsfclient.exe"
    client_sources=("$RSFCLIENT_C")
    [[ -n "$ICON_CLIENT" && -f "$ICON_CLIENT" ]] && client_sources+=("$ICON_CLIENT")
    build_exe "$OUTDIR/rsfclient.exe" "${client_sources[@]}" -- \
        gtk4 gio-2.0 glib-2.0 gdk-pixbuf-2.0
else
    echo "ERROR: cannot find rsfclient source: $RSFCLIENT_C" >&2
    exit 1
fi

log "Copying GLib/GIO helper executables"
for f in \
    gspawn-win64-helper.exe \
    gspawn-win64-helper-console.exe \
    gdbus.exe
do
    copy_if_exists "$UCRT/bin/$f" "$OUTDIR/$f"
done

log "Copying GTK/GNOME runtime resources"
mkdir -p "$OUTDIR/lib" "$OUTDIR/share" "$OUTDIR/etc" "$OUTDIR/tools"

copy_if_exists "$UCRT/lib/gdk-pixbuf-2.0" "$OUTDIR/lib/gdk-pixbuf-2.0"
copy_if_exists "$UCRT/lib/gio" "$OUTDIR/lib/gio"
copy_if_exists "$UCRT/share/glib-2.0" "$OUTDIR/share/glib-2.0"
copy_if_exists "$UCRT/etc/gtk-4.0" "$OUTDIR/etc/gtk-4.0"
copy_if_exists "$UCRT/etc/pango" "$OUTDIR/etc/pango"
copy_if_exists "$UCRT/etc/fonts" "$OUTDIR/etc/fonts"

mkdir -p "$OUTDIR/share/icons" "$OUTDIR/share/themes"
for d in Adwaita hicolor; do
    copy_if_exists "$UCRT/share/icons/$d" "$OUTDIR/share/icons/$d"
done
for d in Default; do
    copy_if_exists "$UCRT/share/themes/$d" "$OUTDIR/share/themes/$d"
done

if [[ "$COPY_GTK_MODULES" == "1" ]]; then
    copy_if_exists "$UCRT/lib/gtk-4.0" "$OUTDIR/lib/gtk-4.0"
    find "$OUTDIR/lib/gtk-4.0" -type d -iname printbackends -prune -exec rm -rf {} + 2>/dev/null || true
fi

copy_if_exists "$UCRT/bin/gdk-pixbuf-query-loaders.exe" "$OUTDIR/tools/gdk-pixbuf-query-loaders.exe"
copy_if_exists "$UCRT/bin/gio-querymodules.exe" "$OUTDIR/tools/gio-querymodules.exe"

if [[ -x "$UCRT/bin/glib-compile-schemas.exe" && -d "$OUTDIR/share/glib-2.0/schemas" ]]; then
    log "Compiling GLib schemas"
    "$UCRT/bin/glib-compile-schemas.exe" "$OUTDIR/share/glib-2.0/schemas" || true
fi

# Avoid shipping build-machine absolute paths. The executables set
# GDK_PIXBUF_MODULEDIR themselves; if a cache is needed later, regenerate it
# inside the target directory.
rm -f "$OUTDIR/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" 2>/dev/null || true

log "Copying DLL dependencies recursively"
declare -A DLL_SOURCE=()

list_dependency_roots() {
    find "$OUTDIR" -maxdepth 1 -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0

    for d in \
        "$OUTDIR/lib/gdk-pixbuf-2.0" \
        "$OUTDIR/lib/gio" \
        "$OUTDIR/lib/gtk-4.0"
    do
        [[ -d "$d" ]] && find "$d" -type f -iname '*.dll' -print0
    done
}

copy_dlls_once() {
    local changed=1
    local f dep dep_path base

    while IFS= read -r -d '' f; do
        while IFS= read -r dep; do
            dep_path="$(awk '
                /=>/ {print $3; next}
                /^\/ucrt64\// {print $1; next}
            ' <<<"$dep")"

            [[ -n "${dep_path:-}" && -f "$dep_path" ]] || continue

            case "$dep_path" in
                "$UCRT"/bin/*.dll|"$UCRT"/lib/*.dll|"$UCRT"/lib/gcc/*/*.dll)
                    ;;
                *)
                    continue
                    ;;
            esac

            base="$(basename "$dep_path")"
            if [[ ! -f "$OUTDIR/$base" ]]; then
                cp -f "$dep_path" "$OUTDIR/$base"
                DLL_SOURCE["$base"]="$dep_path"
                printf '    + %s\n' "$base"
                changed=0
            fi
        done < <(ldd "$f" 2>/dev/null || true)
    done < <(list_dependency_roots)

    return "$changed"
}

for _ in 1 2 3 4 5 6 7 8 9 10; do
    if copy_dlls_once; then
        break
    fi
done

log "Checking for unresolved dependencies"
missing=0
while IFS= read -r -d '' f; do
    if ldd "$f" 2>/dev/null | grep -qi "not found"; then
        echo "Missing dependency for: $f" >&2
        ldd "$f" >&2 || true
        missing=1
    fi
done < <(list_dependency_roots)
if [[ "$missing" == "1" ]]; then
    echo "ERROR: unresolved DLL dependencies remain" >&2
    exit 1
fi

log "Writing package notes and DLL manifest"
{
    echo "RSFPY Windows viewer package"
    echo
    echo "Run directly:"
    echo "    rsfclient.exe"
    echo "    svgviewer.exe file.svg"
    echo
    echo "The executables configure bundled GTK/GdkPixbuf/GSettings paths at startup."
    echo "Keep this whole directory together when copying it to another machine."
    echo
    echo "Remote sender example:"
    echo "    sfspike n1=10 | rsfgraph | rsfclient --send --port 17890"
    echo
    if [[ "$BUILD_X11" == "1" ]]; then
        echo "svgviewer-x11.exe is included for users who explicitly want an X11 backend."
    else
        echo "svgviewer-x11.exe is not included by default; rebuild with BUILD_X11=1 if needed."
    fi
} > "$OUTDIR/README.txt"

{
    echo "# DLLs copied to package root"
    find "$OUTDIR" -maxdepth 1 -type f -iname '*.dll' -printf '%f\n' | sort
} > "$OUTDIR/DLLS.txt"

log "Package contents summary"
du -sh "$OUTDIR" || true
find "$OUTDIR" -maxdepth 2 -type f | sed 's#^#    #'

echo
echo "DONE: $OUTDIR"
echo
echo "Test in MSYS2:"
echo "    $OUTDIR/rsfclient.exe --version"
echo "    $OUTDIR/svgviewer.exe"
echo
echo "Then copy the whole folder to a clean Windows machine and run rsfclient.exe directly."
