SUITE := parlaylib
SUITE_SRC := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
PROGRAMS := BFS pagerank
HELPERS :=
PLAIN_SOURCES :=
include $(SUITE_SRC)/../cilkbench/benchmark.mk
