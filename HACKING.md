## Basic overview

`./configure.py` generates the `build.ninja` files used to build
ninja.  It accepts various flags to adjust build parameters.

The primary build target of interest is `ninja`, but when hacking on
Ninja your changes should be testable so it's more useful to build
and run `ninja_test` when developing.

### Adjusting build flags

Build in "debug" mode while developing (disables optimizations and builds
way faster on Windows):

    ./configure.py --debug

To use clang, set `CXX`:

    CXX=clang++ ./configure.py

## How to successfully make changes to Ninja

Github pull requests are convenient for me to merge (I can just click
a button and it's all handled server-side), but I'm also comfortable
accepting pre-github git patches (via `send-email` etc.).

Good pull requests have all of these attributes:

* Are scoped to one specific issue
* Include a test to demonstrate their correctness
* Update the docs where relevant
* Match the Ninja coding style (see below)
* Don't include a mess of "oops, fix typo" commits

These are typically merged without hesitation.  If a change is lacking
any of the above I usually will ask you to fix it, though there are
obvious exceptions (fixing typos in comments don't need tests).

I am very wary of changes that increase the complexity of Ninja (in
particular, new build file syntax or command-line flags) or increase
the maintenance burden of Ninja.  Ninja is already successfully in use
by hundreds of developers for large projects and it already achieves
(most of) the goals I set out for it to do.  It's probably best to
discuss new feature ideas on the mailing list before I shoot down your
patch.

## Testing

### Test-driven development

Set your build command to

    ./ninja ninja_test && ./ninja_test --gtest_filter=MyTest.Name

now you can repeatedly run that while developing until the tests pass
(I frequently set it as my compilation command in Emacs).  Remember to
build "all" before committing to verify the other source still works!

## Testing performance impact of changes

If you have a Chrome build handy, it's a good test case.  Otherwise,
[the github downoads page](https://github.com/martine/ninja/downloads)
has a copy of the Chrome build files (and depfiles). You can untar
that, then run

    path/to/my/ninja chrome

and compare that against a baseline Ninja.

There's a script at `misc/measure.py` that repeatedly runs a command like
the above (to address variance) and summarizes its runtime.  E.g.

    path/to/misc/measure.py path/to/my/ninja chrome

For changing the depfile parser, you can also build `parser_perftest`
and run that directly on some representative input files.

## Coding guidelines

Generally it's the [Google C++ coding style][], but in brief:

* Function name are camelcase.
* Member methods are camelcase, expect for trivial getters which are
  underscore separated.
* Local variables are underscore separated.
* Member variables are underscore separated and suffixed by an extra
  underscore.
* Two spaces indentation.
* Opening braces is at the end of line.
* Lines are 80 columns maximum.
* All source files should have the Google Inc. license header.

[Google C++ coding style]: http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml

## Documentation

### Style guidelines

* Use `///` for doxygen.
* Use `\a` to refer to arguments.
* It's not necessary to document each argument, especially when they're
  relatively self-evident (e.g. in `CanonicalizePath(string* path, string* err)`,
  the arguments are hopefully obvious)

### Building the manual

    sudo apt-get install asciidoc --no-install-recommends
    ./ninja manual

### Building the code documentation

    sudo apt-get install doxygen
    ./ninja doxygen

## Building for Windows

While developing, it's helpful to copy `ninja.exe` to another name like
`n.exe`; otherwise, rebuilds will be unable to write `ninja.exe` because
it's locked while in use.

### Via Visual Studio

* Install Visual Studio (Express is fine), [Python for Windows][],
  and (if making changes) googletest (see above instructions)
* In a Visual Studio command prompt: `python configure.py --bootstrap`

[Python for Windows]: http://www.python.org/getit/windows/

### Via mingw on Windows (not well supported)

* Install mingw, msys, and python
* In the mingw shell, put Python in your path, and
  `python configure.py --bootstrap`
* To reconfigure, run `python configure.py`
* Remember to strip the resulting executable if size matters to you

### Via mingw on Linux (not well supported)

Setup on Ubuntu Lucid:
* `sudo apt-get install gcc-mingw32 wine`
* `export CC=i586-mingw32msvc-cc CXX=i586-mingw32msvc-c++ AR=i586-mingw32msvc-ar`

Setup on Ubuntu Precise:
* `sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686 wine`
* `export CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++ AR=i686-w64-mingw32-ar`

Setup on Arch:
* Uncomment the `[multilib]` section of `/etc/pacman.conf` and `sudo pacman -Sy`.
* `sudo pacman -S mingw-w64-gcc wine`
* `export CC=x86_64-w64-mingw32-cc CXX=x86_64-w64-mingw32-c++ AR=x86_64-w64-mingw32-ar`
* `export CFLAGS=-I/usr/x86_64-w64-mingw32/include`

Then run:
* `./configure.py --platform=mingw --host=linux`
* Build `ninja.exe` using a Linux ninja binary: `/path/to/linux/ninja`
* Run: `./ninja.exe`  (implicitly runs through wine(!))

### Using Microsoft compilers on Linux (extremely flaky)

The trick is to install just the compilers, and not all of Visual Studio,
by following [these instructions][win7sdk].

[win7sdk]: http://www.kegel.com/wine/cl-howto-win7sdk.html

### Using gcov

Do a clean debug build with the right flags:

    CFLAGS=-coverage LDFLAGS=-coverage ./configure.py --debug
    ninja -t clean ninja_test && ninja ninja_test

Run the test binary to generate `.gcda` and `.gcno` files in the build
directory, then run gcov on the .o files to generate `.gcov` files in the
root directory:

    ./ninja_test
    gcov build/*.o

Look at the generated `.gcov` files directly, or use your favorit gcov viewer.

### Using afl-fuzz

Build with afl-clang++:

    CXX=path/to/afl-1.20b/afl-clang++ ./configure.py
    ninja

Then run afl-fuzz like so:

    afl-fuzz -i misc/afl-fuzz -o /tmp/afl-fuzz-out ./ninja -n -f @@

You can pass `-x misc/afl-fuzz-tokens` to use the token dictionary. In my
testing, that did not seem more effective though.

#### Using afl-fuzz with asan

If you want to use asan (the `isysroot` bit is only needed on OS X; if clang
can't find C++ standard headers make sure your LLVM checkout includes a libc++
checkout and has libc++ installed in the build directory):

    CFLAGS="-fsanitize=address -isysroot $(xcrun -show-sdk-path)" \
        LDFLAGS=-fsanitize=address CXX=path/to/afl-1.20b/afl-clang++ \
        ./configure.py
    AFL_CXX=path/to/clang++ ninja

Make sure ninja can find the asan runtime:

    DYLD_LIBRARY_PATH=path/to//lib/clang/3.7.0/lib/darwin/ \
        afl-fuzz -i misc/afl-fuzz -o /tmp/afl-fuzz-out ./ninja -n -f @@
