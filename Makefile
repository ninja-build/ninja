MAKEFLAGS+= --no-builtin-rules
export MAKE:=${MAKE} -${MAKEFLAGS}

prefix:=/opt/local/bin

.SILENT:
.PHONY: all clean install distclean
all: ninja
	$(prefix)/ninja

ninja: build.ninja

build.ninja: src/depfile_parser.cc src/lexer.cc
	UNUSED_FLAGS='-Weffc++ -Wold-style-cast'
	CFLAGS='-Wall -Wextra -Wundef -Wsign-compare -Wconversion -Wpointer-arith -Wcomment -Wcast-qual -Wcast-align' \
	CXX='$(prefix)/ccache $(prefix)/g++' ./configure.py

src/depfile_parser.cc src/lexer.cc:
	cp -p bin/*.cc src

clean: build.ninja
	-$(prefix)/ninja -t clean

distclean: #XXX clean
	rm -rf CMakeTest/build build *.orig *~ tags ninja *.exe *.pdb *.ninja doc/doxygen/html

install: ninja
	install ninja $(prefix)

Makefile:

