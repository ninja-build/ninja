#!/usr/bin/env python3
"""Generate structured seed corpora for the ninja fuzz targets.

The .ninja_log format read by build_log_fuzzer gates parsing behind the
magic first line "# ninja log v%d" which must carry version 7
(build_log.cc:53-55, :226-235) and five tab-separated fields per entry,
which plain mutation cannot cross. The seeds below are therefore constructed
structurally valid from scratch; the fuzzer then mutates within and across
entries.
"""

import os
import sys

HEADER = "# ninja log v%d\n"


def entry(start, end, mtime, output, cmd_hash):
    return "%d\t%d\t%d\t%s\t%s\n" % (start, end, mtime, output, cmd_hash)


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    seeds = {}

    # Minimal valid v7 log with three well-formed entries.
    seeds["basic"] = (HEADER % 7 + entry(0, 100, 1700000000, "out.o", "deadbeef")
                      + entry(100, 250, 1700000001, "lib.a", "cafebabe")
                      + entry(250, 300, 1700000002, "tool", "1234abcd"))

    # Negative and extreme integer fields (atoi/strtoll/strtoull edges).
    seeds["extremes"] = (HEADER % 7
                         + entry(-2147483648, 2147483647,
                                 9223372036854775807, "ext.o", "ffffffffffffffff")
                         + entry(-1, -1, -9223372036854775808, "neg.o", "0"))

    # Empty numeric fields (atoi("") -> 0 paths) and spaces in outputs.
    seeds["empty-fields"] = (HEADER % 7
                             + "\t\t\t\t\n"
                             + entry(1, 2, 3, "my file.o", "ff")
                             + "\t0\t0\tsp ace.o\t00\n")

    # Entries with too few tab fields (the memchr continue paths), a no-tab
    # line, and an entry without the trailing newline.
    seeds["ragged"] = (HEADER % 7
                       + "only one field\n"
                       + "two\tfields\n"
                       + "three\ttab\tfields\n"
                       + entry(5, 6, 7, "ok.o", "aa")
                       + entry(8, 9, 10, "no-newline.o", "bb").rstrip("\n"))

    # 400 entries over one output: pushes the compaction heuristic
    # (kMinCompactionEntryCount = 100, kCompactionRatio = 3,
    # build_log.cc:314-315) past total > unique * 3.
    seeds["recompaction"] = (HEADER % 7 +
                             "".join(entry(i, i + 1, i, "dup.o", "ab%02x" % (i % 256))
                                     for i in range(400)))

    # Version gates: too old, too new, and headerless input (the version
    # stays 0 and tab-lines still parse as entries).
    seeds["version-old"] = HEADER % 6 + entry(0, 1, 2, "o.o", "aa")
    seeds["version-new"] = HEADER % 8 + entry(0, 1, 2, "o.o", "aa")
    seeds["headerless"] = entry(0, 1, 2, "h.o", "aa") + entry(3, 4, 5, "h2.o", "bb")

    for name, data in seeds.items():
        with open(os.path.join(out_dir, name), "w") as f:
            f.write(data)


if __name__ == "__main__":
    main(sys.argv[1])
