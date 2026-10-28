# Shared build rules for the source snapshot. Invoke make -C <suite>.
# All products, including LLVM IR, live outside the source tree.
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.SECONDARY:
MAKEFLAGS += --no-builtin-rules

include $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../config.mk)
SUITE_BUILD := $(BUILD_ROOT)/benchmarks/$(SUITE)
TIMING_COUNT ?= 1
CODEGEN_FLAGS = -Xclang -disable-llvm-passes
OPTIONS = $(OPT) $(DBG) $(ARCH) -Wall $(ABI_DEF) -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer
BENCH_CPPFLAGS = -I$(CHEETAH_ROOT)/include -I$(RTS_INCLUDE_DIR) -DTIMING_COUNT=$(TIMING_COUNT) $(CPPFLAGS)
ifneq ($(strip $(BASE_CASE)),)
BENCH_CPPFLAGS += -DBASE_CASE=$(BASE_CASE)
endif
ifneq ($(strip $(SERIAL_FLAG)),)
BENCH_CPPFLAGS += -DSERIAL_FLAG=1
endif
RUNTIME_LIBRARY = $(RTS_LIBDIR)/libopencilk.so
ifeq ($(SUITE),handcomp_test)
RUNTIME_LIBRARY = $(RTS_LIBDIR)/libopencilk.a
endif
BENCH_LDLIBS = $(RUNTIME_LIBRARY) $(LIBUNWIND_LOCATION) -lrt -lpthread -lm $(LDLIBS)
BENCH_LINKER = $(CC)
BENCH_LDFLAGS = -no-pie -L$(RTS_LIBDIR) -Wl,-rpath,$(RTS_LIBDIR) $(LDFLAGS)
ifeq ($(SUITE),parlaylib)
BENCH_LINKER = $(CXX)
BENCH_CPPFLAGS += -DPARLAY_OPENCILK -I$(PARLAY_ROOT)/include -I$(PARLAY_ROOT)/examples
BENCH_LDFLAGS += -L$(LIBSTDCXX_PREFIX)/lib64 -L$(LIBSTDCXX_PREFIX)/lib
endif
ifneq ($(SUITE),handcomp_test)
LINKER_SCRIPT = $(SUITE_SRC)/spawn_linker.ld
BENCH_LDFLAGS += -Wl,-T,$(LINKER_SCRIPT)
endif
BENCHMARK_MAKEFILE := $(CHEETAH_ROOT)/cilkbench/benchmark.mk
CONFIG_FILE := $(SUITE_BUILD)/build-options.txt

.PHONY: all list clean run smoke check FORCE $(PROGRAMS)
all: $(addprefix $(SUITE_BUILD)/,$(PROGRAMS))
$(PROGRAMS): %: $(SUITE_BUILD)/%
list:
	@printf '%s\n' '$(SUITE) build targets: $(PROGRAMS)' 'See README.md for preserved drivers that need further porting.'

$(SUITE_BUILD):
	mkdir -p "$@"

# Changing build options must invalidate existing objects, without touching sources.
$(CONFIG_FILE): FORCE | $(SUITE_BUILD)
	@printf '%s\n' '$(CC) $(CXX) $(OPTIONS) $(BENCH_CPPFLAGS) $(CFLAGS) $(CXXFLAGS) $(CODEGEN_FLAGS) $(BENCH_LDFLAGS) $(BENCH_LDLIBS)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

# Cilk lowering is deliberately explicit: preserve Tapir, spawn loops, then
# lower to this research branch's backtracking runtime ABI.
$(SUITE_BUILD)/%.ll: $(SUITE_SRC)/%.c $(CC) $(CONFIG_FILE) $(BENCHMARK_MAKEFILE) $(wildcard $(SUITE_SRC)/*.h) $(CHEETAH_ROOT)/cilkbench/register_spawns.h $(wildcard $(CHEETAH_ROOT)/runtime/*.h)
	$(CC) $(OPTIONS) $(BENCH_CPPFLAGS) $(CFLAGS) -falign-functions=32 -fopencilk -ftapir=none -MMD -MP -MF "$@.d" -MT "$@" -S -emit-llvm "$<" -o "$@"
$(SUITE_BUILD)/%.ll: $(SUITE_SRC)/%.cpp $(CXX) $(CONFIG_FILE) $(BENCHMARK_MAKEFILE) $(wildcard $(SUITE_SRC)/*.h) $(CHEETAH_ROOT)/cilkbench/register_spawns.h $(wildcard $(CHEETAH_ROOT)/runtime/*.h)
	$(CXX) $(OPTIONS) $(BENCH_CPPFLAGS) $(CXXFLAGS) -std=c++17 -falign-functions=32 -fopencilk -ftapir=none -MMD -MP -MF "$@.d" -MT "$@" -S -emit-llvm "$<" -o "$@"
$(SUITE_BUILD)/%.loop.ll: $(SUITE_BUILD)/%.ll $(RTS_BC) $(COPT)
	$(COPT) -opencilk-bt-runtime-bc-path=$(RTS_BC) -S -passes='loop-spawning,default<O3>' "$<" -o "$@"
$(SUITE_BUILD)/%.lowered.ll: $(SUITE_BUILD)/%.loop.ll $(RTS_BC) $(COPT)
	$(COPT) -opencilk-bt-runtime-bc-path=$(RTS_BC) -S -passes='tapir2target,always-inline' -tapir-target=backtrack "$<" -o "$@"
# The pre-lowering O3 pass above is the IR optimization pipeline. Running a
# second generic LLVM pipeline on backtracking callbr IR can invalidate it.
# The preceding always-inline pass materializes ABI helpers needed for linking.
$(SUITE_BUILD)/%.o: $(SUITE_BUILD)/%.lowered.ll $(CC)
	$(CC) $(OPTIONS) $(CODEGEN_FLAGS) -c "$<" -o "$@"

ifneq ($(strip $(PLAIN_SOURCES)),)
$(addprefix $(SUITE_BUILD)/,$(PLAIN_SOURCES:.c=.o)): $(SUITE_BUILD)/%.o: $(SUITE_SRC)/%.c $(CC) $(CONFIG_FILE) $(BENCHMARK_MAKEFILE) $(CHEETAH_ROOT)/runtime/cilk2c_inlined.c $(wildcard $(SUITE_SRC)/*.h) $(CHEETAH_ROOT)/cilkbench/register_spawns.h $(wildcard $(CHEETAH_ROOT)/runtime/*.h)
	$(CC) $(OPTIONS) $(BENCH_CPPFLAGS) $(CFLAGS) -MMD -MP -MF "$@.d" -MT "$@" -c "$<" -o "$@"
endif

$(addprefix $(SUITE_BUILD)/,$(PROGRAMS)): $(SUITE_BUILD)/%: $(SUITE_BUILD)/%.o $(addprefix $(SUITE_BUILD)/,$(HELPERS:.c=.o)) $(RUNTIME_LIBRARY) $(LINKER_SCRIPT) $(BENCH_LINKER)
	$(BENCH_LINKER) $(filter %.o,$^) $(BENCH_LDFLAGS) $(BENCH_LDLIBS) -o "$@"

run: all
	BUILD_ROOT="$(BUILD_ROOT)" CHEETAH_BUILD="$(CHEETAH_BUILD)" DEPS_PREFIX="$(DEPS_PREFIX)" python3 "$(CHEETAH_ROOT)/cilkbench/run.py" --suite $(SUITE) $(RUN_ARGS)
check: smoke
smoke: all
	BUILD_ROOT="$(BUILD_ROOT)" CHEETAH_BUILD="$(CHEETAH_BUILD)" DEPS_PREFIX="$(DEPS_PREFIX)" python3 "$(CHEETAH_ROOT)/cilkbench/run.py" --suite $(SUITE) --smoke $(RUN_ARGS)
clean:
	@test -n "$(SUITE_BUILD)" && test "$(SUITE_BUILD)" != / && rm -rf -- "$(SUITE_BUILD)"

# Follow included runtime and pinned Parlay headers, including compatibility patches.
-include $(wildcard $(SUITE_BUILD)/*.d)
