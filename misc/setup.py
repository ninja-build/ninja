#!/usr/bin/python

from distutils.core import setup
import os
import string
import re


def get_version():
    version = None
    try:
        # First try to parse version from ../src/version.cc
        with open(os.path.join(os.path.dirname(__file__), "..", "src", "version.cc")) as f:
            for line in map(string.strip, f.readlines()):
                m = re.match("const char\* kNinjaVersion = \"(\d+\.\d+\.\d+)\..+\";", line)
                if m:
                    version = m.group(1)
    except IOError:
        pass
    if version is None:
        # If file ../src/version.cc is not available (we are installing from source distribution),
        # look up the previously generated version.py
        version_globals = {}
        execfile(os.path.join(os.path.dirname(__file__), "version.py"), version_globals)
        version = version_globals["__version__"]
    else:
        # If version was parsed from ../src/version.cc, write it to version.py (see use above)
        with open(os.path.join(os.path.dirname(__file__), "version.py"), "w") as f:
            f.write("__version__ = \"" + version + "\"")
    return version


setup(
    name="ninja_syntax",
    version=get_version(),
    description="Python module for generating .ninja files.",
    license="Apache License (2.0)",
    py_modules=["ninja_syntax"],
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
