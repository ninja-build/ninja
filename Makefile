CXXFLAGS := -Wall -g

all: ninja ninja_test
ninja: ninja.cc | Makefile

ninja_test: LDFLAGS = -lgtest -lgtest_main
ninja_test: ninja_test.cc | Makefile
