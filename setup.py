# setup.py
import io
import os
import shlex
import subprocess
from pathlib import Path
import shutil
import numpy
from setuptools import setup, find_packages, Extension
from setuptools.command.install import install
from setuptools.command.develop import develop


here = Path(__file__).resolve().parent

def pkg_config_flags(packages):
    return subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", *packages],
        text=True,
    )


def try_compile_one(name, sources, packages, out):
    """
    Try to compile one svgviewer backend.

    Returns:
        True  if compile succeeded
        False if compile failed
    """
    missing_sources = [src for src in sources if not src.exists()]
    if missing_sources:
        print(f"Skip {name}: missing source file(s):")
        for src in missing_sources:
            print(f"  {src}")
        return False

    try:
        pkg_flags = pkg_config_flags(packages)
    except FileNotFoundError:
        print(f"Skip {name}: pkg-config not found.")
        return False
    except subprocess.CalledProcessError:
        print(f"Skip {name}: failed to get pkg-config flags for: {' '.join(packages)}")
        return False

    cmd = [
        os.environ.get("CC", "cc"),
        *[str(src) for src in sources],
        "-o",
        str(out),
    ] + shlex.split(pkg_flags)

    print(f"Compiling {name}:")
    print(" ".join(shlex.quote(x) for x in cmd))

    try:
        subprocess.check_call(cmd)
    except subprocess.CalledProcessError as e:
        print(f"Failed to compile {name}: {e}")
        return False

    return True


def install_default_viewer(src, dst):
    """
    Copy selected backend binary to the default svgviewer name.
    This is more portable than symlink.
    """
    shutil.copy2(src, dst)

    mode = dst.stat().st_mode
    dst.chmod(mode | 0o111)


def compile_svgviewer(target_dir):
    """
    Compile optional GTK and X11 svgviewer backends.

    Outputs:
        svgviewer-gtk  if GTK build succeeds
        svgviewer-x11  if X11 build succeeds
        svgviewer      default viewer, preferring GTK when available

    At least one backend must compile successfully.
    """
    target_dir = Path(target_dir)
    target_dir.mkdir(parents=True, exist_ok=True)

    tools_dir = here / "src" / "rsfpy" / "tools"

    gtk_sources = [
        tools_dir / "svgviewer.c",
        tools_dir / "svgsequence.c",
    ]

    x11_sources = [
        tools_dir / "svgviewer_x11.c",
        tools_dir / "svgsequence_x11.c",
    ]

    gtk_out = target_dir / "svgviewer-gtk"
    x11_out = target_dir / "svgviewer-x11"
    default_out = target_dir / "svgviewer"

    gtk_ok = try_compile_one(
        name="svgviewer-gtk",
        sources=gtk_sources,
        packages=[
            "gtk4",
            "librsvg-2.0",
            "cairo",
            "glib-2.0",
            "gio-2.0",
        ],
        out=gtk_out,
    )

    gtk_ok = try_compile_one(
        name="rsfclient",
        sources=[tools_dir / "rsfclient.c"],
        packages=[
            "gtk4",
            "glib-2.0",
            "gio-2.0",
        ],
        out=target_dir / "rsfclient",
    )

    x11_ok = try_compile_one(
        name="svgviewer-x11",
        sources=x11_sources,
        packages=[
            "x11",
            "cairo",
            "glib-2.0",
            "librsvg-2.0",
        ],
        out=x11_out,
    )

    if gtk_ok:
        install_default_viewer(gtk_out, default_out)
        print("Default svgviewer -> svgviewer-gtk")
    elif x11_ok:
        install_default_viewer(x11_out, default_out)
        print("Default svgviewer -> svgviewer-x11")
    else:
        raise RuntimeError(
            "Failed to compile svgviewer. "
            "Neither GTK backend nor X11 backend was built successfully. "
            "Please install either GTK4 development files or X11/Cairo/librsvg development files."
        )

def read_requirements():
    req_file = here / "requirements.txt"
    with io.open(req_file, encoding="utf-8") as f:
        return [
            line.strip()
            for line in f
            if line.strip() and not line.startswith("#")
        ]


def read_long_description():
    readme = here / "README.md"
    with io.open(readme, encoding="utf-8") as f:
        return f.read()


def read_version():
    version_ns = {}
    version_file = here / "src" / "rsfpy" / "version.py"
    with open(version_file, encoding="utf-8") as f:
        exec(f.read(), version_ns)
    return version_ns["__version__"]


# def compile_svgviewer(target_dir):
#     """
#     Compile src/rsfpy/tools/svgviewer.c + svgsequence.c into target_dir/svgviewer.
#     """
#     target_dir = Path(target_dir)
#     target_dir.mkdir(parents=True, exist_ok=True)

#     src1 = here / "src" / "rsfpy" / "tools" / "svgviewer.c"
#     src2 = here / "src" / "rsfpy" / "tools" / "svgsequence.c"
#     out = target_dir / "svgviewer"

#     if not src1.exists():
#         raise FileNotFoundError(f"Missing source file: {src1}")
#     if not src2.exists():
#         raise FileNotFoundError(f"Missing source file: {src2}")

#     try:
#         pkg_flags = subprocess.check_output(
#             [
#                 "pkg-config",
#                 "--cflags",
#                 "--libs",
#                 "x11",
#                 "cairo",
#                 "glib-2.0",
#                 "librsvg-2.0",
#             ],
#             text=True,
#         )
#     except FileNotFoundError as e:
#         raise RuntimeError(
#             "pkg-config not found. Please install pkg-config first."
#         ) from e
#     except subprocess.CalledProcessError as e:
#         raise RuntimeError(
#             "Failed to get compile flags from pkg-config. "
#             "Please check that x11, cairo, glib-2.0 and librsvg-2.0 development files are installed."
#         ) from e

#     cmd = [
#         os.environ.get("CC", "cc"),
#         str(src1),
#         str(src2),
#         "-o",
#         str(out),
#     ] + shlex.split(pkg_flags)

#     print("Compiling svgviewer:")
#     print(" ".join(shlex.quote(x) for x in cmd))

#     subprocess.check_call(cmd)


class CustomInstall(install):
    def run(self):
        super().run()

        target_dir = Path(self.install_lib) / "rsfpy" / "bin"
        try:
            compile_svgviewer(target_dir)
        except Exception as e:
            raise RuntimeError(f"Failed to compile svgviewer: {e}") from e


class CustomDevelop(develop):
    def run(self):
        super().run()

        # In editable/develop mode, rsfpy is imported from src/rsfpy,
        # so the binary should be placed under the source package.
        target_dir = here / "src" / "rsfpy" / "bin"
        try:
            compile_svgviewer(target_dir)
        except Exception as e:
            raise RuntimeError(f"Failed to compile svgviewer: {e}") from e


__version__ = read_version()


setup(
    name="rsfpy",
    version=__version__,
    description="A Python toolkit for RSF data IO",
    long_description=read_long_description(),
    long_description_content_type="text/markdown",
    author="jwchen",
    author_email="cplusv_official@qq.com",
    url="https://github.com/cplusv2023/rsfpy",
    license="GPLv2",
    python_requires=">=3",

    packages=find_packages(where="src"),
    package_dir={"": "src"},
    include_package_data=True,

    install_requires=read_requirements(),
    extras_require={
        "rsfsvgpen": ["lxml"],
    },

    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: GPLv2 License",
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
            "rsfmath = rsfpy.tools.Mrsfmath:main",
            "rsfclient = rsfpy.tools.Mrsfclient:main",
        ]
    },

    cmdclass={
        "install": CustomInstall,
        "develop": CustomDevelop,
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