#!/usr/bin/python

from distutils.core import setup


setup(
    name="ninja",
    version="1.5.3",
    description="Python module for generating .ninja files.",
    license="Apache License (2.0)",
    packages=["ninja"],
    package_dir={"ninja": ""},
    maintainer="Marat Dukhan",
    maintainer_email="maratek@gmail.com",
    url="https://github.com/martine/Ninja",
    requires=[],
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Programming Language :: Other",
        "Topic :: Software Development :: Build Tools"
    ])
