MAKEFLAGS+= --no-builtin-rules
export MAKE:=${MAKE} -${MAKEFLAGS}

.SILENT:
.PHONY: all clean install
all:
	ninja

ninja:
	./bootstrap.py

clean:
	ninja -t clean

install: ninja
	sudo install ninja /usr/local/bin

Makefile:

