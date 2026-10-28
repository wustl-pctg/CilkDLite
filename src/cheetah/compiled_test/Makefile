SUITE := compiled_test
SUITE_SRC := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
PROGRAMS := fib cilk_for_test
HELPERS := ktiming.c
PLAIN_SOURCES := ktiming.c
include $(SUITE_SRC)/../cilkbench/benchmark.mk
