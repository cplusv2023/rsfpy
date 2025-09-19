# setup.py
import io, subprocess, os
from setuptools import setup, find_packages
from setuptools.command.install import install
from setuptools.command.develop import develop


# 读取 requirements 文件
with io.open("requirements.txt", encoding="utf-8") as f:
    install_requires = [
        line.strip() for line in f
        if line.strip() and not line.startswith("#")
    ]

# 读取 README 作为 long_description
with io.open("README.md", encoding="utf-8") as f:
    long_description = f.read()

class CustomInstall(install):
    def run(self):
        target_dir = os.path.join(self.install_lib, "rsfpy", "bin")
        os.makedirs(target_dir, exist_ok=True)
        subprocess.check_call(" ".join(["cc", "src/rsfpy/tools/svgviewer.c", "src/rsfpy/tools/svgsequence.c",
                               f"-o {target_dir}/svgviewer "
                               "$(pkg-config --cflags --libs x11 cairo glib-2.0 librsvg-2.0)"]), shell=True)
        super().run()

class CustomDevelop(develop):
    def run(self):
        target_dir = os.path.join(self.install_dir, "rsfpy", "bin")
        os.makedirs(target_dir, exist_ok=True)
        subprocess.check_call(" ".join(["cc", "src/rsfpy/tools/svgviewer.c", "src/rsfpy/tools/svgsequence.c",
                               f"-o {target_dir}/svgviewer "
                               "$(pkg-config --cflags --libs x11 cairo glib-2.0 librsvg-2.0)"]), shell=True)
        super().run()
setup(
    name="rsfpy",
    version="0.1.1",
    description="A Python toolkit for RSF data IO",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="jwchen",
    author_email="cplusv_official@qq.com",
    url="https://github.com/cplusv2023/rsfpy",
    license="GPLv2",
    python_requires=">=3",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    include_package_data=True,
    install_requires=install_requires,
    extra_require={
        "rsfsvgpen": ["lxml"],
    },
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: GPLv2 License",
        "Operating System :: OS Independent",
    ],
    entry_points={
        'console_scripts': [
            'rsfgrey = rsfpy.tools.Mpygrey:main',
            'rsfgraph = rsfpy.tools.Mpygrey:main',
            'rsfwiggle = rsfpy.tools.Mpygrey:main',
            'rsfsvgpen = rsfpy.tools.Msvgpen:main',
            'rsfgrey3 = rsfpy.tools.Mpygrey:main',
            'svgviewer = rsfpy.tools.Msvgviewer:main',
        ]
    },
    cmdclass={
        "install": CustomInstall,
        "develop": CustomDevelop,
    },
)
