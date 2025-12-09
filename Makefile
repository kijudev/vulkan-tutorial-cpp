CXX      := clang++
CXXOPT   := -g -O0 -fno-omit-frame-pointer -fno-optimize-sibling-calls -DDEBUG
CXXFLAGS := -std=c++23 -Wall -Wextra -Iinclude

PKG_CONFIG := pkg-config
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags glfw3 vulkan)
PKG_LIBS   := $(shell $(PKG_CONFIG) --static --libs glfw3 vulkan)

CXXFLAGS += $(PKG_CFLAGS)
LDFLAGS  := $(PKG_LIBS)

LIB_SRCS   := $(wildcard lib/*.cpp)
LIB_OBJS   := $(patsubst lib/%.cpp,bin/obj/%.o,$(LIB_SRCS))

APPS      := $(wildcard apps/*/main.cpp)
APP_BINS  := $(patsubst apps/%/main.cpp,bin/%,$(APPS))

TESTS     := $(wildcard tests/*.cpp)
TEST_BINS := $(patsubst tests/%.cpp,bin/tests/%,$(TESTS))

SHADER_COMPILER := glslc
SHADER_SRC_DIR  := shaders
SHADER_OUT_DIR  := bin/shaders
SHADER_VERT     := $(wildcard $(SHADER_SRC_DIR)/*.vert)
SHADER_FRAG     := $(wildcard $(SHADER_SRC_DIR)/*.frag)
SHADER_SPV      := $(patsubst $(SHADER_SRC_DIR)/%.vert,$(SHADER_OUT_DIR)/%.vert.spv,$(SHADER_VERT)) \
                   $(patsubst $(SHADER_SRC_DIR)/%.frag,$(SHADER_OUT_DIR)/%.frag.spv,$(SHADER_FRAG))

.PHONY: all apps tests shaders run-tests clean compile-commands

all: shaders apps tests

apps: $(APP_BINS)

tests: $(TEST_BINS)

shaders: $(SHADER_SPV)

bin/obj/%.o: lib/%.cpp
	mkdir -p bin/obj
	$(CXX) $(CXXFLAGS) $(CXXOPT) $(LDFLAGS) -c $< -o $@

bin/%: apps/%/main.cpp $(LIB_OBJS)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(CXXOPT) $< $(LIB_OBJS) $(LDFLAGS) -o $@

bin/tests/%: tests/%.cpp $(LIB_OBJS)
	mkdir -p bin/tests
	$(CXX) $(CXXFLAGS) $(CXXOPT) $< $(LIB_OBJS) $(LDFLAGS) -o $@

# Shader compilation rules (support .vert and .frag)
$(SHADER_OUT_DIR)/%.vert.spv: $(SHADER_SRC_DIR)/%.vert
	mkdir -p $(SHADER_OUT_DIR)
	$(SHADER_COMPILER) -o $@ $<

$(SHADER_OUT_DIR)/%.frag.spv: $(SHADER_SRC_DIR)/%.frag
	mkdir -p $(SHADER_OUT_DIR)
	$(SHADER_COMPILER) -o $@ $<

clean:
	rm -rf bin
	rm -f compile_commands.json

compile-commands: clean
	bear -- make

run-tests: tests
	@set -e; \
	for t in $(TEST_BINS); do \
	  printf "Running %s\n" "$$t"; \
	  if "$$t"; then \
	    printf "PASS$\n"; \
	  else \
	    printf "FAIL: %s\n" "$$t"; \
	    exit 1; \
	  fi \
	done
