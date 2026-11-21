#!/usr/bin/env python3
"""Generate structured seed corpora for the ninja fuzz targets.

The .ninja_deps format read by deps_log_fuzzer gates every byte behind checks
plain mutation cannot cross: the "# ninjadeps\\n" magic (deps_log.cc:38), the
version word 4 (deps_log.cc:41), per-record 4-byte size words with a high-bit
record-type flag, 4-byte alignment padding, and a ~id checksum per path
record (deps_log.cc:277). The seeds below are therefore constructed
structurally valid from scratch; the fuzzer then mutates within and across
records.
"""

import os
import struct
import sys

MAGIC = b"# ninjadeps\n"
VERSION = 4


def path_record(node_id, path):
    raw = path.encode()
    # Mirror DepsLog::RecordId (deps_log.cc:411): size = path + pad + 4-byte
    # checksum, padded to a 4-byte boundary; checksum = ~id.
    pad = (-(len(raw))) % 4
    size = len(raw) + pad + 4
    body = raw + b"\0" * pad + struct.pack("<I", ~node_id & 0xFFFFFFFF)
    return struct.pack("<I", size) + body


def deps_record(out_id, mtime, dep_ids):
    # Mirror DepsLog::RecordDeps (deps_log.cc:96): out_id, 64-bit mtime split
    # into two words, then the dependency ids; size has the high bit set.
    size = 4 * (3 + len(dep_ids))
    body = struct.pack("<iII", out_id, mtime & 0xFFFFFFFF, mtime >> 32)
    body += b"".join(struct.pack("<i", d) for d in dep_ids)
    return struct.pack("<I", size | 0x80000000) + body


def log(records):
    return MAGIC + struct.pack("<i", VERSION) + b"".join(records)


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    seeds = {}

    # Minimal valid log: two path records and one deps record.
    seeds["basic"] = log([
        path_record(0, "foo.o"),
        path_record(1, "foo.cc"),
        deps_record(0, 0x5F000000, [1]),
    ])

    # Several dependencies and a full 64-bit mtime.
    seeds["multi-deps"] = log(
        [path_record(i, "src/file%d.cc" % i) for i in range(5)] + [
            path_record(5, "out.o"),
            deps_record(5, 0x123456789ABCDEF0, [0, 1, 2, 3, 4]),
        ])

    # Deps record with zero dependencies (deps_count == 0 path).
    seeds["empty-deps"] = log([
        path_record(0, "empty.o"),
        deps_record(0, 0, []),
    ])

    # Same out_id recorded twice: exercises UpdateDeps deduplication
    # (unique_dep_record_count in the compaction heuristic).
    seeds["dedup"] = log([
        path_record(0, "dup.o"),
        path_record(1, "dup.cc"),
        deps_record(0, 100, [1]),
        deps_record(0, 200, [1]),
    ])

    # Path lengths 1..6 hit the trailing-NUL trimming loop (deps_log.cc:262).
    seeds["padding"] = log([path_record(i, "a" * (1 + i)) for i in range(6)])

    # Many distinct deps records: pushes total_dep_record_count past the
    # needs_recompaction heuristic (kMinCompactionEntryCount = 1000,
    # deps_log.cc:313).
    n = 1010
    seeds["recompaction"] = log(
        [path_record(i, "gen/node%d.o" % i) for i in range(n)] +
        [deps_record(i, i, [(i + 1) % n]) for i in range(n)])

    for name, data in seeds.items():
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main(sys.argv[1])
