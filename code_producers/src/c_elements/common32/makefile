CC=g++
CFLAGS=-std=c++11 -O3 -I.
EXTRA_FILES =
DEPS_HPP = circom.hpp calcwit.hpp fr.hpp
DEPS_CPP := $(filter-out $(EXTRA_FILES), $(wildcard *.cpp))
DEPS_O := $(patsubst %.cpp,%.o,$(DEPS_CPP))
LINKED_DEPS_O := $(patsubst %.cpp,%.linked.o,$(DEPS_CPP))

all: {{run_name}}

%.o: %.cpp $(DEPS_HPP)
	$(CC) -c $< $(CFLAGS)

%.linked.o: %.cpp $(DEPS_HPP)
	$(CC) -c $< $(CFLAGS) -UNDEBUG -DCIRCOM_LINKED_WITNESS_ONLY -o $@

{{run_name}}: $(DEPS_O) {{run_name}}.o
	$(CC) -o {{run_name}} $(DEPS_O) -lgmp {{#if has_parallelism}}-pthread{{/if}}

lib{{run_name}}_linked.a: $(LINKED_DEPS_O) {{run_name}}.linked.o
	ar rcs lib{{run_name}}_linked.a $(LINKED_DEPS_O)
