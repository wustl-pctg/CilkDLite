SUITE := cilkbench
SUITE_SRC := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
PROGRAMS := fib nqueens cilksort fft cholesky lu heat strassen matmul rectmul cilk_for_test
HELPERS := getoptions.c
PLAIN_SOURCES := getoptions.c
include $(SUITE_SRC)/../cilkbench/benchmark.mk
