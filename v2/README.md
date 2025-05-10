# Ninja 2.0

This directory contains a work-in-progress prototype of the next major version of Ninja. It reuses
parts of v1, replaces some, and currently duplicates some stuff (mostly ninja.cc).

To build v2 run:

```
cmake -Bbuild-cmake -DNINJA_V2=1
```

The 2.0 binary is called `nj`.

## Breaking changes for users

**DISCLAIMER: Nothing is set in stone!**

* [x] Update status periodically to fix #2515.
* [x] Introduce fancy status output (colors, cycle through running jobs, ...) by default.
* [ ] Make description part of the status format string in order to allow to change its position,
      see #1045
* [ ] Introduce a `--status` command line flag, see #1180, and remove support for `NINJA_STATUS` env
      var.
* [ ] Add support for `NINJA_FLAGS` #797. That's the replacement for `NINJA_STATUS` then, too.
* [ ] Always realpath everything in order to fix #1251.
* [ ] Only use hashes instead of timestamps, see #1459.
* [ ] To mitigate the performance impact of the previous two points: Add client/server mode.
* [ ] Look at a lot more places for build manifest, e.g. in `build/` or parent directories, see
      #1328.

Have a look at https://github.com/ninja-build/ninja/issues?q=milestone%3A2.0.0%20 for a complete
list.

## Changes for developers of Ninja itself

* [x] Use C++23
* [ ] Replace getopt with a good C++ arg parsing library. getopt has caused problems in the past.
* [ ] When doing that, also improve the argument names / format / etc.
* [ ] Have very strict clang-tidy rules

## TODO

* [ ] Remove Boost and use Fuchsia's AsyncLoop implementation instead, see
      https://github.com/ninja-build/ninja/pull/2516#issuecomment-2449927610
