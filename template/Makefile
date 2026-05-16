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

clean:
	rm -rf $(BUILD)
