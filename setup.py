import io
import os
import shlex
import shutil
import subprocess
import sys
import warnings
from pathlib import Path

import numpy
from setuptools import Command, Extension, find_packages, setup
from setuptools.command.build_py import build_py


HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
TOOLS_DIR = SRC_DIR / "rsfpy" / "tools"


def env_bool(name, default=False):
    value = os.environ.get(name)
    if value is None:
        return default

    return value.strip().lower() in {"1", "true", "yes", "on", "y"}


def run_pkg_config(packages):
    pkg_config = os.environ.get("PKG_CONFIG", "pkg-config")
    return subprocess.check_output(
        [pkg_config, "--cflags", "--libs", *packages],
        text=True,
    )


def executable_mode(path):
    path.chmod(path.stat().st_mode | 0o111)


def compile_executable(name, sources, packages, output, extra_ldflags=None):
    missing_sources = [src for src in sources if not src.exists()]
    if missing_sources:
        raise FileNotFoundError(
            f"{name}: missing source file(s): "
            + ", ".join(str(src) for src in missing_sources)
        )

    pkg_flags = shlex.split(run_pkg_config(packages)) if packages else []
    compiler = shlex.split(os.environ.get("CC", "cc"))
    cflags = shlex.split(os.environ.get("CFLAGS", ""))
    ldflags = shlex.split(os.environ.get("LDFLAGS", ""))
    extra_ldflags = extra_ldflags or []

    cmd = [
        *compiler,
        "-std=gnu99",
        "-O2",
        *cflags,
        *(str(src) for src in sources),
        "-o",
        str(output),
        *pkg_flags,
        *extra_ldflags,
        *ldflags,
    ]

    print(f"building native tool {name}")
    print(" ".join(shlex.quote(part) for part in cmd))
    subprocess.check_call(cmd)
    executable_mode(output)


NATIVE_TOOLS = {
    "svgviewer-gtk": {
        "sources": [
            TOOLS_DIR / "svgviewer.c",
            TOOLS_DIR / "svgsequence.c",
        ],
        "darwin_sources": [
            TOOLS_DIR / "svgviewer_clipboard_macos.m",
        ],
        "darwin_ldflags": [
            "-framework",
            "AppKit",
            "-framework",
            "Foundation",
        ],
        "packages": [
            "gtk4",
            "librsvg-2.0",
            "cairo",
            "glib-2.0",
            "gio-2.0",
        ],
    },
    "svgviewer-x11": {
        "sources": [
            TOOLS_DIR / "svgviewer_x11.c",
            TOOLS_DIR / "svgsequence_x11.c",
        ],
        "packages": [
            "x11",
            "cairo",
            "glib-2.0",
            "librsvg-2.0",
        ],
    },
    "rsfclient": {
        "sources": [
            TOOLS_DIR / "rsfclient.c",
        ],
        "packages": [
            "gtk4",
            "glib-2.0",
            "gio-2.0",
        ],
    },
    "vpl2svg": {
        "sources": [
            TOOLS_DIR / "vpl2svg.c",
        ],
        "packages": [],
        "ldflags": [
            "-lm",
        ],
    },
}


def build_native_tools(target_dir, required=False):
    """
    Build optional bundled executables into rsfpy/bin.

    Missing system libraries should not prevent the core Python API from being
    installed. Set RSFPY_REQUIRE_NATIVE=1 when packaging a release that must
    include the native viewer/client programs.
    """
    target_dir = Path(target_dir)
    target_dir.mkdir(parents=True, exist_ok=True)

    if os.name == "nt":
        message = "native rsfpy tools are not built from source on Windows"
        if required:
            raise RuntimeError(message)
        warnings.warn(message)
        return {}

    built = {}
    failures = {}

    for name, spec in NATIVE_TOOLS.items():
        output = target_dir / name
        sources = list(spec["sources"])
        extra_ldflags = list(spec.get("ldflags", []))
        if sys.platform == "darwin":
            sources.extend(spec.get("darwin_sources", []))
            extra_ldflags.extend(spec.get("darwin_ldflags", []))

        try:
            compile_executable(
                name=name,
                sources=sources,
                packages=spec["packages"],
                output=output,
                extra_ldflags=extra_ldflags,
            )
        except (FileNotFoundError, subprocess.CalledProcessError) as exc:
            failures[name] = str(exc)
            warnings.warn(f"could not build optional native tool {name}: {exc}")
        else:
            built[name] = output

    default_viewer = target_dir / "svgviewer"
    if "svgviewer-gtk" in built:
        shutil.copy2(built["svgviewer-gtk"], default_viewer)
        executable_mode(default_viewer)
        built["svgviewer"] = default_viewer
    elif "svgviewer-x11" in built:
        shutil.copy2(built["svgviewer-x11"], default_viewer)
        executable_mode(default_viewer)
        built["svgviewer"] = default_viewer

    if required:
        required_names = {"svgviewer", "rsfclient", "vpl2svg"}
        missing = sorted(required_names.difference(built))
        if missing:
            details = "\n".join(
                f"  {name}: {failures.get(name, 'not built')}"
                for name in sorted(failures)
            )
            raise RuntimeError(
                "failed to build required native rsfpy tool(s): "
                + ", ".join(missing)
                + ("\n" + details if details else "")
            )

    if not built:
        warnings.warn(
            "no optional native rsfpy tools were built; Python APIs and "
            "pure-Python command wrappers are still installed"
        )

    return built


class BuildPy(build_py):
    def run(self):
        super().run()

        if env_bool("RSFPY_BUILD_NATIVE", True):
            target_dir = Path(self.build_lib) / "rsfpy" / "bin"
            build_native_tools(
                target_dir=target_dir,
                required=env_bool("RSFPY_REQUIRE_NATIVE", False),
            )
        else:
            print("skipping optional native rsfpy tools (RSFPY_BUILD_NATIVE=0)")


class BuildNative(Command):
    description = "build optional rsfpy native tools"
    user_options = [
        ("inplace", "i", "build tools into src/rsfpy/bin"),
        ("required", None, "fail if svgviewer, rsfclient, and vpl2svg cannot be built"),
    ]
    boolean_options = ["inplace", "required"]

    def initialize_options(self):
        self.inplace = False
        self.required = False
        self.build_lib = None

    def finalize_options(self):
        self.set_undefined_options("build_py", ("build_lib", "build_lib"))

    def run(self):
        target_dir = (
            SRC_DIR / "rsfpy" / "bin"
            if self.inplace
            else Path(self.build_lib) / "rsfpy" / "bin"
        )
        build_native_tools(target_dir=target_dir, required=self.required)


def read_requirements(exclude=()):
    excluded = set(exclude)
    req_file = HERE / "requirements.txt"
    with io.open(req_file, encoding="utf-8") as handle:
        return [
            line.strip()
            for line in handle
            if line.strip()
            and not line.startswith("#")
            and line.strip() not in excluded
        ]


def read_long_description():
    readme = HERE / "README.md"
    with io.open(readme, encoding="utf-8") as handle:
        return handle.read()


def read_version():
    version_ns = {}
    version_file = SRC_DIR / "rsfpy" / "version.py"
    with open(version_file, encoding="utf-8") as handle:
        exec(handle.read(), version_ns)
    return version_ns["__version__"]


setup(
    name="rsfpy",
    version=read_version(),
    description="A Python toolkit for RSF data IO",
    long_description=read_long_description(),
    long_description_content_type="text/markdown",
    author="jwchen",
    author_email="cplusv_official@qq.com",
    url="https://github.com/cplusv2023/rsfpy",
    license="GPL-2.0-only",
    python_requires=">=3",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    include_package_data=True,
    package_data={
        "rsfpy.bin": ["*"],
    },
    install_requires=read_requirements(exclude={"lxml"}),
    extras_require={
        "rsfsvgpen": ["lxml"],
        "plot": ["matplotlib"],
    },
    classifiers=[
        "Programming Language :: Python :: 3",
        "Operating System :: OS Independent",
    ],
    entry_points={
        "console_scripts": [
            "rsfgrey = rsfpy.tools.Mpygrey:main",
            "rsfgraph = rsfpy.tools.Mpygrey:main",
            "rsfwiggle = rsfpy.tools.Mpygrey:main",
            "rsfsvgpen = rsfpy.tools.Msvgpen:main",
            "rsfgrey3 = rsfpy.tools.Mpygrey:main",
            "svgviewer = rsfpy.tools.Msvgviewer:main",
            "vplviewer = rsfpy.tools.Mvplviewer:main",
            "rsfvpl2svg = rsfpy.tools.Mrsfvpl2svg:main",
            "rsfmath = rsfpy.tools.Mrsfmath:main",
            "rsfclient = rsfpy.tools.Mrsfclient:main",
        ]
    },
    cmdclass={
        "build_py": BuildPy,
        "build_native": BuildNative,
    },
    ext_modules=[
        Extension(
            "rsfpy.plot.rsfpy_utils",
            sources=["src/rsfpy/plot/utils.c"],
            include_dirs=[numpy.get_include()],
            extra_compile_args=["-O3"],
        )
    ],
)
