MAKEFLAGS+= --no-builtin-rules
export MAKE:=${MAKE} -${MAKEFLAGS}

.SILENT:
.PHONY: all clean install distclean
all: ninja
	ninja

ninja: build.ninja

build.ninja: src/depfile_parser.cc src/lexer.cc
	./configure.py
#XXX	./bootstrap.py

clean:
	-ninja -t clean

distclean: #XXX clean
	rm -rf CMakeTest/build build *.orig *~ tags ninja *.exe *.pdb *.ninja doc/doxygen/html

install: ninja
	install ninja /usr/local/bin

Makefile:

