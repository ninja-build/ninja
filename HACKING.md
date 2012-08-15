
### Adjusting build flags

    CFLAGS=-O3 ./configure.py

### Testing

#### Installing gtest

* On older Ubuntus it'll install as libraries into `/usr/lib`:

        apt-get install libgtest

* On newer Ubuntus it's only distributed as source

        apt-get install libgtest-dev
        ./configure --with-gtest=/usr/src/gtest

* Otherwise you need to download it, unpack it, and pass --with-gtest
  as appropriate.

#### Test-driven development

Set your build command to

    ./ninja ninja_test && ./ninja_test --gtest_filter=MyTest.Name

now you can repeatedly run that while developing until the tests pass.
Remember to build "all" before committing to verify the other source
still works!

### Testing performance impact of changes

If you have a Chrome build handy, it's a good test case.
Otherwise, https://github.com/martine/ninja/downloads has a copy of
the Chrome build files (and depfiles). You can untar that, then run

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
* Member variables are underscore separated and suffixed by an extra underscore.
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

## Building on Windows

While developing, it's helpful to copy `ninja.exe` to another name like
`n.exe`; otherwise, rebuilds will be unable to write `ninja.exe` because
it's locked while in use.

### Via Visual Studio

* Install Visual Studio (Express is fine), [Python for Windows][],
  and (if making changes) googletest (see above instructions)
* In a Visual Studio command prompt: `python bootstrap.py`

[Python for Windows]: http://www.python.org/getit/windows/

### Via mingw on Linux (not well supported)

* `sudo apt-get install gcc-mingw32 wine`
* `export CC=i586-mingw32msvc-cc CXX=i586-mingw32msvc-c++ AR=i586-mingw32msvc-ar`
* `./configure.py --platform=mingw --host=linux`
* Build `ninja.exe` using a Linux ninja binary: `/path/to/linux/ninja`
* Run: `./ninja.exe`  (implicitly runs through wine(!))

### Via mingw on Windows (not well supported)
* Install mingw, msys, and python
* In the mingw shell, put Python in your path, and: python bootstrap.py
* To reconfigure, run `python configure.py`
* Remember to strip the resulting executable if size matters to you

## Clang

Enable colors manually via `-fcolor-diagnostics`:

    CXX='/path/to/llvm/Release+Asserts/bin/clang++ -fcolor-diagnostics' ./configure.py
