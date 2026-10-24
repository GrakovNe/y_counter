CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined
SRC      := ../../src

.PHONY: test clean

test: test_appconf test_history_ring
	./test_appconf
	./test_history_ring

test_appconf: test_appconf.cpp tinytest.h $(SRC)/appconf.cpp $(SRC)/appconf.h
	$(CXX) $(CXXFLAGS) -I$(SRC) -o $@ test_appconf.cpp $(SRC)/appconf.cpp

test_history_ring: test_history_ring.cpp tinytest.h $(SRC)/history_ring.cpp $(SRC)/history_ring.h
	$(CXX) $(CXXFLAGS) -I$(SRC) -o $@ test_history_ring.cpp $(SRC)/history_ring.cpp

clean:
	rm -rf test_appconf test_history_ring *.dSYM
