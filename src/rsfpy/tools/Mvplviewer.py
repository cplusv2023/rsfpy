#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__doc__ = """
\033[1mNAME\033[0m
    \tMvplviewer.py
\033[1mDESCRIPTION\033[0m
    \tView VPL images by converting them to SVG first.

    \tThe original VPL filename is kept in the SVG sequence metadata so the
    \tviewer still reports the source VPL file, not a temporary SVG file.

\033[1mSYNOPSIS\033[0m
    \tMvplviewer.py [--backend client|gtk|x11|auto] [conversion options] [< in0.vpl] in1.vpl ...

\033[1mSTREAMING\033[0m
    \tcachepipe=y
    \t    Match xtpen's default pipe behavior. Piped stdin is cached first,
    \t    then processed before command-line VPL files. With the GTK backend,
    \t    vpl2svg --stream still emits completed frames as it reads the cache.
    \t    Use cachepipe=n for direct live stdin streaming.

\033[1mCONVERSION OPTIONS\033[0m
    \tbgcolor=white, --bgcolor white
    \t    Fill the converted SVG canvas. Accepts matplotlib-like short names
    \t    (r, b, k), common names (red, blue), and #rgb/#rgba/#rrggbb/#rrggbbaa.

    \tfont=, fontfamily=, fontsz=, fontcolor=, fontfat=/fontweight=
    \t    Global text defaults. More specific title*/label*/barlabel*/
    \t    scalebar* options override these values.
    \t    font/fontfamily aliases: 1=default sans stack, 2=serif stack,
    \t    3=monospace stack.

    \tframecolor=, framefat=/framewidth=
    \t    Global frame/axis/grid defaults.

    \taxiscolor=, gridcolor=, labelcolor=, titlecolor=
    \t    Group-specific overrides. color/col accept named colors, #hex,
    \t    grayscale floats, or VPL palette indexes.

\033[1mENVIRONMENT\033[0m
    \tRSFVPLOPTS
    \t    Default VPL conversion options, for example:
    \t    RSFVPLOPTS='bgcolor=k fontsz=15 axiscolor=#cccccc'

    \tRSFPY_VPLVIEWER_CONVERTER
    \t    VPL-to-SVG converter command. Default: bundled vpl2svg.

    \tRSFPY_SVGVIEWER_BACKEND
    \t    Default backend: auto, client, gtk, or x11.

\033[1mMORE INFO\033[0m
    \tAuthor:\tauthor_label
    \tEmail:\temail_label
    \tSource:\tgithub_label
\033[1mVERSION\033[0m
    \tversion_label
"""

import os
import re
import sys
import shlex
import shutil
import tempfile
from subprocess import PIPE, run, Popen
from textwrap import dedent

from rsfpy.tools import Msvgviewer as svgviewer
from rsfpy.utils import _get_stdname, _str_match_re
from rsfpy.version import __version__, __email__, __author__, __github__


__progname__ = os.path.basename(sys.argv[0])

__doc__ = __doc__.replace("author_label", __author__)
__doc__ = __doc__.replace("email_label", __email__)
__doc__ = __doc__.replace("github_label", __github__)
__doc__ = __doc__.replace("version_label", __version__)

DOC = dedent(__doc__.replace("Mvplviewer.py", __progname__))

CONVERTER_PREFIXES = (
    "font",
    "label",
    "title",
    "barlabel",
    "scalebar",
    "frame",
    "axis",
    "grid",
)

TEXT_PREFIXES = {"font", "label", "title", "barlabel", "scalebar"}
LINE_PREFIXES = {"frame", "axis", "grid"}

TEXT_SUFFIXES = (
    "color",
    "col",
    "sz",
    "size",
    "fat",
    "weight",
)

LINE_SUFFIXES = (
    "color",
    "col",
    "fat",
    "width",
)

CONVERTER_OPTION_NAMES = {"bgcolor", "border", "font", "fontfamily"}

for _prefix in CONVERTER_PREFIXES:
    _suffixes = TEXT_SUFFIXES if _prefix in TEXT_PREFIXES else LINE_SUFFIXES
    for _suffix in _suffixes:
        CONVERTER_OPTION_NAMES.add(_prefix + _suffix)
    if _prefix in TEXT_PREFIXES:
        CONVERTER_OPTION_NAMES.add(_prefix + "family")

CONVERTER_OPTION_FLAGS = {"--" + name for name in CONVERTER_OPTION_NAMES}
STREAM_STDIN = object()


def falsey(value):
    return str(value).strip().lower() in ("0", "false", "no", "off", "n")


def cachepipe_value(items):
    value = os.environ.get("RSFPY_VPLVIEWER_CACHEPIPE", "y")
    for item in items:
        if "=" not in item:
            continue
        parsed = _str_match_re([item])
        key, parsed_value = next(iter(parsed.items()), ("", ""))
        if key == "cachepipe":
            value = parsed_value

    return not falsey(value)


def strip_split_markers(svg):
    return re.sub(br'(?m)^<!-- RSFPY_SPLIT[^\n]*-->\n?', b"", svg)


def split_svg_frames(svg):
    parts = re.split(br'(?=<!-- RSFPY_SPLIT\b)', svg)
    frames = []
    for part in parts:
        part = part.strip()
        if not part:
            continue
        part = strip_split_markers(part).strip()
        if part:
            frames.append(part + b"\n")
    if not frames and svg.strip():
        frames.append(strip_split_markers(svg).strip() + b"\n")
    return frames


def converter_command():
    override = os.environ.get("RSFPY_VPLVIEWER_CONVERTER")
    if override:
        cmd = svgviewer.split_cmd(override)
        if cmd:
            return cmd

    bundled = svgviewer.resource_path("vpl2svg")
    if svgviewer.is_executable(bundled):
        return [str(bundled)]

    path = shutil.which("vpl2svg")
    if path:
        return [path]

    raise RuntimeError(
        "vpl2svg converter not found. Build native tools or set "
        "RSFPY_VPLVIEWER_CONVERTER."
    )


def cached_stdin_path(stdin_data, temp_paths):
    fd, path = tempfile.mkstemp(prefix=".vplstream_", suffix=".vpl")
    with os.fdopen(fd, "wb") as f:
        f.write(stdin_data)
    temp_paths.append(path)
    return path


def read_vpl_sources(args, stdin_data, *, cachepipe=True, temp_paths=None):
    sources = []
    temp_paths = temp_paths if temp_paths is not None else []

    if stdin_data is STREAM_STDIN:
        sources.append(("stdin", None, "stdin", None))
    elif stdin_data is not None:
        if cachepipe:
            path = cached_stdin_path(stdin_data, temp_paths)
            sources.append(("stdin", None, "stdin", path))
        else:
            sources.append(("stdin", stdin_data, "stdin", None))

    for item in args:
        if item == "-":
            if stdin_data is None:
                raise RuntimeError("'-' was specified but stdin is empty")
            continue

        try:
            with open(item, "rb") as f:
                if cachepipe:
                    data = None
                else:
                    data = f.read()
        except OSError:
            continue

        title = os.path.splitext(os.path.basename(item))[0] or "figure"
        sources.append((title, data, item, item))

    return sources


def converter_supports_path_arg(cmd):
    if not cmd:
        return False
    return os.path.basename(cmd[0]).lower().startswith("vpl2svg")


def append_progress_line(progress_path, message):
    if not progress_path or not message:
        return
    try:
        with open(progress_path, "a", encoding="utf-8") as f:
            f.write(str(message).rstrip() + "\n")
            f.flush()
    except OSError:
        pass


def converter_args_for_progress(cmd, converter_args, progress_path):
    if not progress_path or not converter_supports_path_arg(cmd):
        return list(converter_args)
    if "--verbose" in converter_args:
        return list(converter_args)
    return ["--verbose"] + list(converter_args)


def convert_one_vpl(
    cmd,
    converter_args,
    title,
    data,
    source_path,
    convert_path=None,
    progress_path=None,
):
    use_path = (
        convert_path is not None
        and os.path.exists(convert_path)
        and converter_supports_path_arg(cmd)
    )
    stderr_target = PIPE
    progress_file = None

    if progress_path:
        try:
            progress_file = open(progress_path, "ab", buffering=0)
            stderr_target = progress_file
        except OSError:
            progress_file = None
            stderr_target = PIPE

    try:
        result = run(
            cmd + converter_args + [convert_path] if use_path else cmd + converter_args,
            input=None if use_path else data,
            stdout=-1,
            stderr=stderr_target,
        )
    finally:
        if progress_file:
            progress_file.close()

    if getattr(result, "stderr", None):
        try:
            sys.stderr.buffer.write(result.stderr)
        except Exception:
            sys.stderr.write(result.stderr.decode("utf-8", "replace"))

    if result.returncode != 0:
        raise RuntimeError(
            "VPL conversion failed for %s with code %s"
            % (source_path, result.returncode)
        )

    svg = result.stdout
    if b"<svg" not in svg[:4096]:
        raise RuntimeError("VPL conversion did not produce SVG for %s" % source_path)

    return svg


def build_svg_payload(args, stdin_data, converter_args, cachepipe=True):
    if stdin_data is STREAM_STDIN:
        raise RuntimeError("streaming stdin is only supported by the GTK streaming backend")

    temp_paths = []
    sources = read_vpl_sources(args, stdin_data, cachepipe=cachepipe, temp_paths=temp_paths)
    if not sources:
        raise RuntimeError("no VPL input")

    cmd = converter_command()
    chunks = []

    try:
        for title, data, source_path, convert_path in sources:
            svg = convert_one_vpl(cmd, converter_args, title, data, source_path, convert_path)
            if b"<!-- RSFPY_SPLIT" in svg:
                chunks.append(svg)
                if not svg.endswith(b"\n"):
                    chunks.append(b"\n")
            else:
                chunks.append(svgviewer.frame_marker(title, source_path))
                chunks.append(svg)
                chunks.append(b"\n")
    finally:
        for path in temp_paths:
            try:
                os.unlink(path)
            except OSError:
                pass

    if len(sources) == 1:
        return sources[0][0], b"".join(chunks)

    return "sequence_%d" % len(sources), b"".join(chunks)


def send_payload_with_client(title, payload):
    cmd_base = svgviewer.client_sender_command()

    if not svgviewer.command_exists(cmd_base):
        sys.stderr.write(
            "Client sender not found: %s\n"
            % " ".join(shlex.quote(x) for x in cmd_base)
        )
        return 127

    try:
        host, port, token, cfg_path = svgviewer.client_connection_options()
        cmd = list(cmd_base)
        cmd += ["--host", host, "--port", str(port)]
        if token:
            cmd += ["--token", token]
        if title:
            cmd += ["--title", title]

        if cfg_path:
            sys.stderr.write("Using rsfclient pair config: %s\n" % cfg_path)

        result = run(cmd, input=payload, capture_output=False)

        if result.returncode != 0 and cfg_path:
            svgviewer.remove_stale_pair_config(cfg_path)

        return result.returncode

    except Exception as exc:
        sys.stderr.write("Client sender failed: %s\n" % exc)
        return 1


def run_svg_payload_viewer(backend, title, payload):
    tried = []

    for name, path in svgviewer.get_backend_order(backend):
        if name == "CLIENT":
            ret = send_payload_with_client(title, payload)
            if ret == 0:
                return 0

            tried.append("CLIENT: exited with code %s: %s" % (
                ret,
                " ".join(shlex.quote(x) for x in svgviewer.client_sender_command()),
            ))

            if backend == "client":
                return ret

            continue

        if not svgviewer.is_executable(path):
            tried.append("%s: not found or not executable: %s" % (name, path))
            continue

        result = run(
            [str(path)],
            input=payload,
            capture_output=False,
            env=svgviewer.viewer_env_for_backend(name),
        )

        if result.returncode == 0:
            return 0

        tried.append("%s: exited with code %s: %s" % (
            name,
            result.returncode,
            path,
        ))

        return result.returncode

    sys.stderr.write("No usable SVG viewer backend succeeded.\n")
    for item in tried:
        sys.stderr.write("  %s\n" % item)

    return 1


def streaming_backend(backend):
    for name, path in svgviewer.get_backend_order(backend):
        if name == "CLIENT":
            return None, None
        if name == "GTK" and svgviewer.is_executable(path):
            return name, path
        return None, None
    return None, None


def append_manifest_line(manifest, svg_path, source_path, label):
    manifest.write(
        "%s\t%s\t%s\n"
        % (
            svg_path,
            source_path if source_path else svg_path,
            label if label else "Frame",
        )
    )
    manifest.flush()
    try:
        os.fsync(manifest.fileno())
    except OSError:
        pass


def write_stream_frames(
    tempdir,
    manifest,
    source_title,
    source_path,
    svg,
    start_index,
    progress_path=None,
):
    frames = split_svg_frames(svg)
    if not frames:
        frames = [svg]

    count = 0
    for local_index, frame in enumerate(frames, 1):
        frame_index = start_index + count
        svg_path = os.path.join(tempdir, "frame_%06d.svg" % frame_index)
        label = source_title
        if len(frames) > 1:
            label = "%s #%d" % (source_title, local_index)

        with open(svg_path, "wb") as f:
            f.write(frame)

        append_manifest_line(manifest, svg_path, source_path, label)
        count += 1
        append_progress_line(
            progress_path,
            "vplviewer: loaded frame %d from %s" % (frame_index + 1, source_path),
        )

    return count


def iter_svg_frames_from_stream(pipe):
    frame = bytearray()
    saw_marker = False

    for line in iter(pipe.readline, b""):
        if line.startswith(b"<!-- RSFPY_SPLIT"):
            if frame:
                yield bytes(frame)
                frame.clear()
            saw_marker = True
        frame.extend(line)

    if frame:
        yield bytes(frame)
    elif not saw_marker:
        return


def stream_convert_one_vpl_to_manifest(
    cmd,
    converter_args,
    tempdir,
    manifest,
    title,
    data,
    source_path,
    convert_path,
    start_index,
    progress_path=None,
):
    progress_file = None
    if source_path == "stdin" and data is None and convert_path is None:
        try:
            progress_file = open(progress_path, "ab", buffering=0) if progress_path else None
        except OSError:
            progress_file = None
        proc = Popen(
            cmd + ["--stream"] + converter_args,
            stdin=sys.stdin.buffer,
            stdout=PIPE,
            stderr=progress_file if progress_file else None,
        )
    elif not convert_path or not os.path.exists(convert_path) or not converter_supports_path_arg(cmd):
        svg = convert_one_vpl(
            cmd,
            converter_args,
            title,
            data,
            source_path,
            convert_path,
            progress_path=progress_path,
        )
        return write_stream_frames(
            tempdir,
            manifest,
            title,
            source_path,
            svg,
            start_index,
            progress_path=progress_path,
        )
    else:
        try:
            progress_file = open(progress_path, "ab", buffering=0) if progress_path else None
        except OSError:
            progress_file = None
        proc = Popen(
            cmd + ["--stream"] + converter_args + [convert_path],
            stdout=PIPE,
            stderr=progress_file if progress_file else None,
        )

    count = 0
    try:
        for frame in iter_svg_frames_from_stream(proc.stdout):
            if b"<svg" not in frame[:8192]:
                continue
            count += write_stream_frames(
                tempdir,
                manifest,
                title,
                source_path,
                frame,
                start_index + count,
                progress_path=progress_path,
            )

        ret = proc.wait()
    finally:
        if proc.stdout:
            proc.stdout.close()
        if progress_file:
            progress_file.close()

    if ret != 0:
        raise RuntimeError(
            "VPL streaming conversion failed for %s with code %s"
            % (source_path, ret)
        )

    if count == 0:
        raise RuntimeError("VPL streaming conversion produced no SVG frames for %s" % source_path)

    return count


def run_streaming_svg_viewer(backend, args, stdin_data, converter_args, cachepipe=True):
    name, path = streaming_backend(backend)
    if name != "GTK":
        title, payload = build_svg_payload(args, stdin_data, converter_args, cachepipe=cachepipe)
        return run_svg_payload_viewer(backend, title, payload)

    tempdir = tempfile.mkdtemp(prefix="rsfpy-vplstream-")
    manifest_path = os.path.join(tempdir, "frames.manifest")
    progress_path = os.path.join(tempdir, "progress.log")
    temp_paths = []
    viewer = None
    frame_index = 0

    try:
        append_progress_line(progress_path, "vplviewer: preparing VPL inputs")
        with open(manifest_path, "w", encoding="utf-8") as manifest:
            viewer = Popen(
                [str(path), "--watch", manifest_path, "--progress", progress_path],
                env=svgviewer.viewer_env_for_backend(name),
            )

            sources = read_vpl_sources(
                args,
                stdin_data,
                cachepipe=cachepipe,
                temp_paths=temp_paths,
            )
            if not sources:
                raise RuntimeError("no VPL input")

            cmd = converter_command()
            progress_converter_args = converter_args_for_progress(
                cmd,
                converter_args,
                progress_path,
            )
            total_sources = len(sources)
            for source_index, (title, data, source_path, convert_path) in enumerate(sources, 1):
                if viewer.poll() is not None:
                    return viewer.returncode

                append_progress_line(
                    progress_path,
                    "vplviewer: converting %d/%d: %s"
                    % (source_index, total_sources, source_path),
                )
                frame_index += stream_convert_one_vpl_to_manifest(
                    cmd,
                    progress_converter_args,
                    tempdir,
                    manifest,
                    title,
                    data,
                    source_path,
                    convert_path,
                    frame_index,
                    progress_path=progress_path,
                )
                append_progress_line(
                    progress_path,
                    "vplviewer: finished %d/%d: %s"
                    % (source_index, total_sources, source_path),
                )

            append_progress_line(progress_path, "vplviewer: conversion complete")

        return viewer.wait() if viewer else 1

    except Exception:
        if viewer and viewer.poll() is None:
            viewer.terminate()
        raise
    finally:
        for temp_path in temp_paths:
            try:
                os.unlink(temp_path)
            except OSError:
                pass
        shutil.rmtree(tempdir, ignore_errors=True)


def show_help(exit_code=0):
    try:
        run(["less", "-R"], input=DOC.encode())
    except Exception:
        sys.stdout.write(DOC)
    sys.exit(exit_code)


def split_vpl_args(items, *, allow_files=True):
    files = []
    converter_args = []
    i = 0

    while i < len(items):
        item = items[i]
        if item.startswith("--"):
            if (
                "=" not in item
                and i + 1 < len(items)
                and not items[i + 1].startswith("--")
                and "=" not in items[i + 1]
            ):
                i += 2
            else:
                i += 1
            continue

        if "=" in item and not os.path.exists(item):
            parsed = _str_match_re([item])
            key, value = next(iter(parsed.items()), ("", ""))
            if key in CONVERTER_OPTION_NAMES:
                converter_args.extend(["--" + key, value])
            i += 1
            continue

        if allow_files and not item.startswith("--"):
            files.append(item)
        i += 1

    return files, converter_args


def collect_env_converter_args():
    raw = os.environ.get("RSFVPLOPTS", "")
    if not raw.strip():
        return []
    parsed = _str_match_re(raw)
    return [
        item
        for key, value in parsed.items()
        for item in (("--" + key), value)
        if key in CONVERTER_OPTION_NAMES
    ]


def main():
    backend, raw_args, help_requested = svgviewer.parse_common_cli()

    if help_requested:
        show_help(0)

    cachepipe = cachepipe_value(raw_args)
    args, key_value_converter_args = split_vpl_args(raw_args)
    converter_args = collect_env_converter_args() + key_value_converter_args
    stdin_data = None

    stdname = _get_stdname()
    if (
        stdname[1]
        and os.path.exists(stdname[1])
        and stdname[1].lower().endswith((".v", ".vpl"))
    ):
        args = [stdname[1]] + args
    elif not sys.stdin.isatty():
        name, _ = streaming_backend(backend)
        if name == "GTK" and not cachepipe:
            stdin_data = STREAM_STDIN
        else:
            stdin_data = sys.stdin.buffer.read()

    args = svgviewer.readable_input_args(args)

    try:
        ret = run_streaming_svg_viewer(
            backend,
            args,
            stdin_data,
            converter_args,
            cachepipe=cachepipe,
        )
    except Exception as exc:
        sys.stderr.write("vplviewer: %s\n" % exc)
        ret = 1

    sys.exit(ret)


if __name__ == "__main__":
    main()
