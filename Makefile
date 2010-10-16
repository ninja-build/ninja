CC=$(CXX)
CXXFLAGS := -Wall -g

all_i_currently_care_about: ninja_test

ninja_test: LDFLAGS = -lgtest -lgtest_main
ninja_test: ninja_test.o
ninja_test.o: ninja_test.cc ninja.h

ninja: ninja.o
ninja.o: ninja.cc ninja.h
