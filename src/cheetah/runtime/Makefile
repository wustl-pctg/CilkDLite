include ../config.mk

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

OBJ_DIR := $(CHEETAH_BUILD)/obj
SOURCES := $(filter-out cilkfor.c personality-c.c personality-cpp.c,$(wildcard *.c))
OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SOURCES))
PERSON_C_OBJ := $(OBJ_DIR)/personality-c.o
PERSON_CPP_OBJ := $(OBJ_DIR)/personality-cpp.o
MAIN := $(RTS_LIBDIR)/$(RTS_LIB)
PERSON_C := $(RTS_LIBDIR)/$(RTS_C_PERSONALITY_LIB)
PERSON_CPP := $(RTS_LIBDIR)/$(RTS_CXX_PERSONALITY_LIB)
LIBRARIES := $(MAIN).a $(MAIN).so $(PERSON_C).a $(PERSON_C).so $(PERSON_CPP).a $(PERSON_CPP).so
HEADER_SOURCES := $(wildcard ../include/cilk/*.h)
HEADERS := $(patsubst ../include/%,$(RTS_INCLUDE_DIR)/%,$(HEADER_SOURCES))
RUNTIME_FLAGS := $(OPT) $(DBG) $(ARCH) -Wall -Werror -fPIC -fno-omit-frame-pointer
RUNTIME_DEFS := -DREDUCER_MODULE $(ABI_DEF)
RUNTIME_INCLUDES := -I../include
CONFIG_STAMP := $(OBJ_DIR)/.build-config

.PHONY: all clean check-compiler FORCE

all: $(LIBRARIES) $(RTS_BC) $(HEADERS)

check-compiler:
	test -x "$(CC)" || { printf 'OpenCilk compiler not found: %s\nBuild the compiler first or set COMPILER_BIN.\n' "$(CC)" >&2; exit 1; }

$(OBJ_DIR) $(RTS_LIBDIR) $(RTS_INCLUDE_DIR)/cilk:
	mkdir -p "$@"

$(CONFIG_STAMP): FORCE | $(OBJ_DIR)
	@printf '%s\n' '$(CC) $(AR) $(CPPFLAGS) $(RUNTIME_INCLUDES) $(RUNTIME_DEFS) $(RUNTIME_FLAGS) $(CFLAGS) $(LDFLAGS) $(LIBUNWIND_LOCATION)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

$(OBJ_DIR)/%.o: %.c ../config.mk Makefile $(CONFIG_STAMP) $(CC) | $(OBJ_DIR) check-compiler
	"$(CC)" $(CPPFLAGS) $(RUNTIME_INCLUDES) $(RUNTIME_DEFS) $(RUNTIME_FLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MAIN).a: $(OBJECTS) $(CONFIG_STAMP) | $(RTS_LIBDIR)
	$(AR) rcs "$@" $(OBJECTS)

$(MAIN).so: $(OBJECTS) $(CONFIG_STAMP) | $(RTS_LIBDIR) check-compiler
	"$(CC)" -shared -Wl,-soname,$(notdir $@) $(LDFLAGS) -o "$@" $(OBJECTS) $(LIBUNWIND_LOCATION) -lpthread -ldl

$(PERSON_C).a: $(PERSON_C_OBJ) $(CONFIG_STAMP) | $(RTS_LIBDIR)
	$(AR) rcs "$@" "$<"

$(PERSON_C).so: $(PERSON_C_OBJ) $(CONFIG_STAMP) | $(RTS_LIBDIR) check-compiler
	"$(CC)" -shared -Wl,-soname,$(notdir $@) $(LDFLAGS) -o "$@" "$<"

$(PERSON_CPP).a: $(PERSON_CPP_OBJ) $(CONFIG_STAMP) | $(RTS_LIBDIR)
	$(AR) rcs "$@" "$<"

$(PERSON_CPP).so: $(PERSON_CPP_OBJ) $(CONFIG_STAMP) | $(RTS_LIBDIR) check-compiler
	"$(CC)" -shared -Wl,-soname,$(notdir $@) $(LDFLAGS) -o "$@" "$<"

$(RTS_BC): cilk2c_inlined.c ../config.mk Makefile $(CONFIG_STAMP) $(CC) | $(RTS_LIBDIR) $(OBJ_DIR) check-compiler
	"$(CC)" $(CPPFLAGS) $(RUNTIME_INCLUDES) -O3 -DCHEETAH_API= -DCHEETAH_INTERNAL_NORETURN='__attribute__((noreturn))' -DCHEETAH_INTERNAL= -DCILK_DEBUG=0 -MMD -MP -MF "$(OBJ_DIR)/cilk2c_inlined-abi.d" -MT "$@" -c -emit-llvm "$<" -o "$@"

$(RTS_INCLUDE_DIR)/cilk/%.h: ../include/cilk/%.h | $(RTS_INCLUDE_DIR)/cilk
	cp "$<" "$@"

clean:
	$(RM) $(OBJECTS) $(PERSON_C_OBJ) $(PERSON_CPP_OBJ) $(LIBRARIES) $(RTS_BC) $(HEADERS) $(OBJECTS:.o=.d) $(PERSON_C_OBJ:.o=.d) $(PERSON_CPP_OBJ:.o=.d) $(OBJ_DIR)/cilk2c_inlined-abi.d $(CONFIG_STAMP)

-include $(OBJECTS:.o=.d) $(PERSON_C_OBJ:.o=.d) $(PERSON_CPP_OBJ:.o=.d) $(OBJ_DIR)/cilk2c_inlined-abi.d
