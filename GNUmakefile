MAKEFLAGS+= --no-builtin-rules
export MAKE:=${MAKE} -${MAKEFLAGS}

prefix?=/opt/local

bindir:=$(prefix)/bin
libdir:=$(prefix)/lib
includedir:=$(prefix)/include

.SILENT:
.PHONY: all test install clean distclean help
all: ninja

ninja: build.ninja
	$(bindir)/ninja

build.ninja: src/depfile_parser.cc src/lexer.cc
	CPPFLAGS="-I$(includedir)" \
	CXXFLAGS='-Wall -Wextra -Weffc++ -Wold-style-cast -Wcast-qual' \
	CFLAGS="-Wundef -Wsign-compare -Wconversion -Wpointer-arith -Wcomment -Wcast-align" \
	LDFLAGS="-L$(libdir)" \
	CXX='$(bindir)/ccache $(bindir)/g++' ./configure.py ###XXX --debug

src/depfile_parser.cc src/lexer.cc:
	cp -p bin/*.cc src

test: ninja_test
	./$<

ninja_test: ninja
	./ninja $@

help: ninja
	./ninja -t targets

clean: build.ninja
	-$(bindir)/ninja -t clean

distclean: #XXX clean
	rm -rf CMakeTest/build build *.orig *~ tags ninja ninja_test *.exe *.pdb *.ninja doc/doxygen/html

install: ninja
	install ninja $(bindir)

Makefile:

