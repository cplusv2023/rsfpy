# setup.py
import io
from setuptools import setup, find_packages

# 读取 requirements 文件
with io.open("requirements.txt", encoding="utf-8") as f:
    install_requires = [
        line.strip() for line in f
        if line.strip() and not line.startswith("#")
    ]

# 读取 README 作为 long_description
with io.open("README.md", encoding="utf-8") as f:
    long_description = f.read()

setup(
    name="rsfpy",
    version="0.1.0",
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
            'rsfgrey = Mpygrey:main',
            'rsfgraph = Mpygrey:main',
            'rsfwiggle = Mpygrey:main',
            'rsfsvgpen = Msvgpen:main',
        ]
    },
)
