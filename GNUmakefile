MAKEFLAGS+= --no-builtin-rules
export MAKE:=${MAKE} -${MAKEFLAGS}

prefix?=/opt/local

bindir:=$(prefix)/bin
libdir:=$(prefix)/lib
includedir:=$(prefix)/include

gtestdir:=$(shell $(bindir)/grealpath $(HOME)/Workspace/cpp/gtest-1.6.0)

###XXX .SILENT:
.PHONY: test manual install clean distclean help ### all
.DEFAULT: all
all::

# bootstrap without installed ninja!
bootstrap.py: ;
ninja.bootstrap: bootstrap.py
	./$<
	cp -p -n ninja $@
	-$(RM) build.ninja

# bootstrap with install ninja!
ninja: ninja.bootstrap build.ninja
	./$<

manual:: README.html
README.html: README HACKING GNUmakefile $(bindir)/rst2html-2.7.py
	$(bindir)/rst2html-2.7.py -dg $< > $@

###FIXME -Wundef not usable with gtest-1.6.0! ck
###XXX	CPPFLAGS="-I$(gtestdir)/include -I$(includedir) -DUSE_TIME_T" \
###TODO CXXFLAGS are not used! ck

build.ninja: src/depfile_parser.cc src/lexer.cc
	CPPFLAGS="-I$(gtestdir)/include -I$(includedir)" \
	CXXFLAGS='-Wall -Wextra -Weffc++ -Wold-style-cast -Wcast-qual -Wundef -std=c++11' \
	CFLAGS='-Wsign-compare -Wconversion -Wpointer-arith -Wcomment -Wcast-align -Wcast-qual' \
	LDFLAGS="-L$(libdir)" \
	CXX="$(prefix)/libexec/ccache/g++" ./configure.py --debug --with-gtest=$(gtestdir)

src/depfile_parser.in.cc: ;
src/depfile_parser.cc: src/depfile_parser.in.cc $(bindir)/re2c
	$(bindir)/re2c -b -i --no-generation-date -o $@ $<

src/lexer.in.cc: ;
src/lexer.cc: src/lexer.in.cc $(bindir)/re2c
	$(bindir)/re2c -b -i --no-generation-date -o $@ $<

test:: ninja_test
	./$<

test:: parser_perftest
	./$< build/*.d

test:: build_log_perftest
	./$<

test:: hash_collision_bench
	./$<

help: ninja
	./ninja -t targets

clean: build.ninja
	-$(RM) build/*.o ###XXX build.ninja

distclean: ###XXX clean
	find . \( -name '*~' -o -name '.*~' -o -name '*.pyc' \) -delete
	rm -rf CMakeTest/build build *.orig *~ tags ninja ninja_test *_perftest \
		hash_collision_bench *.exe *.pdb *.ninja doc/doxygen/html *.html
	git status --ignored --short

install: ninja
	install ninja $(bindir)

# NoOp rules for extern dependencies
GNUmakefile: ;
$(bindir)/ninja: ;
$(bindir)/re2c: ;
$(bindir)/rst2html-2.7.py: ;

# Anything we don't know how to build will use this rule.
#
% :: ninja
	./ninja $@
