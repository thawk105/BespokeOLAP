CXX        := g++
CXXSTD     := -std=c++20
OPTFLAGS   := -O2 -g
WARNS      := -Wall -Wextra
ARROW_CF   := $(shell pkg-config --cflags arrow parquet)
ARROW_LD   := $(shell pkg-config --libs arrow parquet)

CXXFLAGS   := $(CXXSTD) $(OPTFLAGS) $(WARNS) -I. $(ARROW_CF)
LDFLAGS    := $(ARROW_LD) -ldl -lpthread

BUILD      := build

# Host binary (db): only db.cpp + utils/build_id.cpp need to be linked.
# The loader/builder/query implementations are loaded at runtime via dlopen
# (see docs/PHASE2.md; the plugin mechanism is exercised in Phase 3).
DB_SRCS    := db.cpp utils/build_id.cpp
DB_OBJS    := $(patsubst %.cpp,$(BUILD)/%.o,$(DB_SRCS))
DB         := $(BUILD)/db

# Test runner uses the same dlopen pattern; libmylib*.so are built separately
# in Phase 3. For Phase 2 we just compile the binary so the build is exercised.
TEST_SRCS  := test/main.cpp utils/build_id.cpp
TEST_OBJS  := $(patsubst %.cpp,$(BUILD)/%.o,$(TEST_SRCS))
TEST       := $(BUILD)/test_runner

.PHONY: all clean test
all: $(DB)

$(DB): $(DB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build-only (does not execute); test runner needs libmylib*.so at runtime.
test: $(TEST)

$(TEST): $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) -o $@

# Synthesis build (Phase 3a+): combine db_static.cpp with synth-generated
# loader/builder/query impls from $(SYNTH_DIR).
#
# Usage: make synth SYNTH_DIR=../output/q1
#
# Output: $(BUILD)/db_synth, invoked as `db_synth --data-dir <dir> --query <id>`.
# Empty/missing SYNTH_DIR fails the link (load/build/query unresolved), which
# is the expected dry-run smoke-test behavior.
SYNTH_DIR     ?=
SYNTH_SRCS    := $(wildcard $(SYNTH_DIR)/*.cpp)
SYNTH_OBJS    := $(patsubst %.cpp,$(BUILD)/synth/%.o,$(notdir $(SYNTH_SRCS)))

SYNTH_MAIN_SRCS := db_static.cpp utils/build_id.cpp
SYNTH_MAIN_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SYNTH_MAIN_SRCS))

DB_SYNTH      := $(BUILD)/db_synth

.PHONY: synth
synth: $(DB_SYNTH)

$(DB_SYNTH): $(SYNTH_MAIN_OBJS) $(SYNTH_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD)/synth/%.o: $(SYNTH_DIR)/%.cpp
	@mkdir -p $(BUILD)/synth
	$(CXX) $(CXXFLAGS) -I$(SYNTH_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD)
