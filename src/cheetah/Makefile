.DEFAULT_GOAL := all
.PHONY: all runtime benchmarks cilkbench parlaylib compiled-tests handcomp-tests check clean help

all: runtime benchmarks compiled-tests

runtime:
	$(MAKE) -C runtime

benchmarks: cilkbench parlaylib

cilkbench parlaylib: runtime
	$(MAKE) -C $@

compiled-tests: runtime
	$(MAKE) -C compiled_test

handcomp-tests: runtime
	$(MAKE) -C handcomp_test

check: compiled-tests
	$(MAKE) -C compiled_test check

clean:
	$(MAKE) -C compiled_test clean
	$(MAKE) -C handcomp_test clean
	$(MAKE) -C cilkbench clean
	$(MAKE) -C parlaylib clean
	$(MAKE) -C runtime clean

help:
	@printf '%s\n' \
	  'make -j<JOBS>                Build runtime, Cilkbench, supported Parlay benchmarks, and compiled tests' \
	  'make runtime                Build only the backtracking runtime' \
	  'make benchmarks             Build Cilkbench and supported Parlay benchmarks' \
	  'make compiled-tests         Build the small compiler/runtime examples' \
	  'make handcomp-tests         Build the handwritten runtime tests' \
	  'make check                  Run the small compiler/runtime checks' \
	  'make clean                  Remove generated runtime and benchmark products' \
	  'Override COMPILER_BIN, BUILD_ROOT, DEPS_PREFIX, or PARLAY_ROOT on the make command line.'
