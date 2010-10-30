CC=$(CXX)
CXXFLAGS := -Wall -g

all_i_currently_care_about: ninja_test

OBJS=parsers.o ninja_jumble.o
TESTS=parsers_test.o ninja_test.o

ninja_test: LDFLAGS = -lgtest -lgtest_main -lpthread
ninja_test: $(OBJS) $(TESTS)

ninja: ninja.o $(OBJS)
