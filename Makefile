CXXFLAGS := -Wall -g

all_i_currently_care_about: ninja_test
ninja: ninja.cc | Makefile ninja.h

ninja_test: LDFLAGS = -lgtest -lgtest_main
ninja_test: ninja_test.cc ninja.h | Makefile
