# Shared paths for the runtime and benchmark Makefiles. Override these on the
# make command line; generated files belong to the release's build directory.
CHEETAH_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PROJECT_ROOT ?= $(abspath $(CHEETAH_ROOT)/../..)
BUILD_ROOT ?= $(PROJECT_ROOT)/build
COMPILER_BIN ?= $(BUILD_ROOT)/opencilk/bin
DEPS_PREFIX ?= $(BUILD_ROOT)/deps
PARLAY_ROOT ?= $(PROJECT_ROOT)/src/parlaylib

GLIBC_PREFIX ?= $(DEPS_PREFIX)/glibc
LIBSTDCXX_PREFIX ?= $(DEPS_PREFIX)/libstdcxx
GPERFTOOLS_PREFIX ?= $(DEPS_PREFIX)/gperftools

CC = $(COMPILER_BIN)/clang
CXX = $(COMPILER_BIN)/clang++
COPT = $(COMPILER_BIN)/opt
LINK_CC = $(CC)
LLVM_LINK = $(COMPILER_BIN)/llvm-link
LLVM_CONFIG = $(COMPILER_BIN)/llvm-config

# Backtracking currently supports x86-64 Linux. This explicit default also lets
# make clean/help work before the compiler has been built.
TARGET_TRIPLE ?= x86_64-unknown-linux-gnu
CHEETAH_BUILD ?= $(BUILD_ROOT)/cheetah
RESOURCE_DIR = $(CHEETAH_BUILD)
RTS_LIBDIR_NAME = lib/$(TARGET_TRIPLE)
RTS_LIBDIR = $(CHEETAH_BUILD)/$(RTS_LIBDIR_NAME)
RTS_INCLUDE_DIR = $(CHEETAH_BUILD)/include
RTS_LIB = libopencilk
RTS_C_PERSONALITY_LIB = libopencilk-personality-c
RTS_CXX_PERSONALITY_LIB = libopencilk-personality-cpp
RTS_BC = $(RTS_LIBDIR)/$(RTS_LIB)-abi.bc
RTS_OPT = -fopencilk --opencilk-resource-dir=$(RESOURCE_DIR)
ABI_DEF = -DOPENCILK_ABI

# Use the host/container's libunwind. Custom libc, libstdc++, and allocator
# installations are selected by the benchmark runner, never by a global LD_*.
LIBUNWIND_LOCATION ?= -lunwind
OPT ?= -O3
DBG ?= -g3
ARCH ?=
TIMING_COUNT ?= 1
MANYPROC ?= 2

