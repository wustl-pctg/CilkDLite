SUITE := handcomp_test
SUITE_SRC := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
PROGRAMS := fib mm_dac
HELPERS := ktiming.c getoptions.c ZERO.c
PLAIN_SOURCES := fib.c mm_dac.c ktiming.c getoptions.c ZERO.c
include $(SUITE_SRC)/../cilkbench/benchmark.mk
