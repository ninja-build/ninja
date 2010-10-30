CC=$(CXX)
CXXFLAGS := -Wall -g

all_i_currently_care_about: ninja_test

OBJS=parsers.o ninja_jumble.o
TESTS=parsers_test.o ninja.o

ninja_test: LDFLAGS = -lgtest -lgtest_main -lpthread
ninja_test: $(OBJS) $(TESTS)
ninja_test.o: ninja_test.cc

ninja: ninja.o $(OBJS)
ninja.o: ninja.cc
