#!/usr/bin/env python3
"""Generate structured seed corpora for the ninja fuzz targets.

The dyndep format applied by dyndep_fuzzer requires the keyword
"ninja_dyndep_version" with a supported version, and each "build <out>:
dyndep" line must name the output of an edge whose manifest build statement
declares a dyndep binding for that file — none of which plain mutation can
cross. Each seed below is therefore a valid (manifest, dyndep file) pair,
framed the way dyndep_fuzzer.cc frames its input: two big-endian bytes of
manifest length, the manifest, then the dyndep file contents.
"""

import os
import struct
import sys

DYNDep_PATH = "/tmp/fuzz.dyndep.dd"

MANIFEST = ("rule r\n"
            "  command = cc -c $in -o $out\n"
            "build out.o: r in.c\n"
            "  dyndep = %s\n" % DYNDep_PATH)


def frame(dyndep_text, manifest=MANIFEST):
    manifest_bytes = manifest.encode()
    return struct.pack(">H", len(manifest_bytes)) + manifest_bytes + dyndep_text.encode()


def dyndep_version():
    return "ninja_dyndep_version = 1\n"


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    seeds = {}

    # One edge, one implicit input; the canonical dyndep round trip.
    seeds["basic"] = frame(dyndep_version() + "build out.o: dyndep | imp.c\n")

    # Implicit outputs via |@ and a restat binding.
    seeds["implicit-outputs"] = frame(
        dyndep_version() +
        "build out.o: dyndep | imp.c |@ imp_out.o\n"
        "  restat = 1\n")

    # Two edges sharing the dyndep file, both mentioned in it.
    manifest2 = ("rule r\n"
                 "  command = cc -c $in -o $out\n"
                 "build a.o: r a.c\n"
                 "  dyndep = %s\n"
                 "build b.o: r b.c\n"
                 "  dyndep = %s\n" % (DYNDep_PATH, DYNDep_PATH))
    seeds["two-edges"] = frame(
        dyndep_version() +
        "build a.o: dyndep | a-imp.c\n"
        "build b.o: dyndep | b-imp.c\n",
        manifest=manifest2)

    # Multiple implicit inputs on one edge.
    seeds["many-inputs"] = frame(
        dyndep_version() +
        "build out.o: dyndep | i1.c i2.c i3.c i4.c i5.c\n")

    for name, data in seeds.items():
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main(sys.argv[1])
