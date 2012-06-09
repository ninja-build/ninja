MAKEFLAGS+= --no-builtin-rules
export MAKE:=${MAKE} -${MAKEFLAGS}

prefix?=/opt/local

bindir:=$(prefix)/bin
libdir:=$(prefix)/lib
includedir:=$(prefix)/include

.SILENT:
.PHONY: all test manual install clean distclean help
all: ninja

manual:: README.html
README.html: README HACKING GNUmakefile
	$(bindir)/rst2html-2.7.py -dg $< > $@

ninja: build.ninja $(bindir)/ninja
	$(bindir)/ninja -d explain

build.ninja: src/depfile_parser.cc src/lexer.cc
	CPPFLAGS="-I$(includedir)" \
	CXXFLAGS='-Wall -Wextra -Weffc++ -Wold-style-cast -Wcast-qual' \
	CFLAGS="-Wundef -Wsign-compare -Wconversion -Wpointer-arith -Wcomment -Wcast-align" \
	LDFLAGS="-L$(libdir)" \
	CXX='$(bindir)/ccache $(bindir)/g++' ./configure.py ###XXX --debug

src/depfile_parser.cc: src/depfile_parser.in.cc
	$(bindir)/re2c -b -i --no-generation-date -o $@ $<

src/lexer.cc: src/lexer.in.cc
	$(bindir)/re2c -b -i --no-generation-date -o $@ $<

test: ninja_test
	./$<

ninja_test: ninja
	./ninja $@

help: ninja
	./ninja -t targets

clean: build.ninja
	-$(bindir)/ninja -t clean

distclean: #XXX clean
	rm -rf CMakeTest/build build *.orig *~ tags ninja ninja_test *.exe *.pdb *.ninja doc/doxygen/html *.html

install: ninja
	install ninja $(bindir)

GNUmakefile:

# Anything we don't know how to build will use this rule.
#
% :: ;
	./ninja $@


