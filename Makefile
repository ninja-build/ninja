CXXFLAGS := -Wall -g

all: ninja ninja_test
ninja: ninja.cc ninja.h | Makefile

ninja_test: LDFLAGS = -lgtest -lgtest_main
ninja_test: ninja_test.cc ninja.h | Makefile
