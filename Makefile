# ===========================================================================
#  navtools_create_mesh - Makefile (Linux / gcc, 64-bit)
#
#  Builds a headless tool that links against the Source SDK 2013 tier /
#  appframework libraries and drives the dedicated engine to generate nav
#  meshes.  See README.md for the big picture and docs/ARCHITECTURE.md for how
#  the pieces fit together.
#
#  This Makefile is the "direct" build path: it compiles tier1 and mathlib
#  straight from the vendored SDK sources and links against the SDK's prebuilt
#  libtier0.so / libvstdlib.so / appframework.a.  It does NOT require the
#  podman + Steam Runtime container.  For the canonical Steam-Runtime build,
#  use ./buildallprojects (see scripts/build_steamrt.sh and README.md).
#
#  Quick start:
#     git submodule update --init --recursive
#     make
#     ./build/navtools_create_mesh -game <moddir> -map <map> -basedir <SDKBase>
# ===========================================================================

# --- Paths -----------------------------------------------------------------
SDK         ?= external/source-sdk-2013
SDK_SRC     := $(SDK)/src
SDK_LIB     ?= $(SDK_SRC)/lib/public/linux64

SRC_DIR     := src/navtools_create_mesh
BUILD_DIR   := build
TARGET      := $(BUILD_DIR)/navtools_create_mesh

# --- Toolchain -------------------------------------------------------------
CXX ?= g++

INCLUDES := \
	-I$(SRC_DIR) \
	-I$(SDK_SRC)/public \
	-I$(SDK_SRC)/public/tier0 \
	-I$(SDK_SRC)/public/tier1 \
	-I$(SDK_SRC)/public/mathlib \
	-I$(SDK_SRC)/common \
	-I$(SDK_SRC)/mathlib

# Match the Source SDK 2013 / Steam runtime build settings.  The prebuilt
# libtier0.so / libvstdlib.so use the pre-C++11 libstdc++ ABI, so we must build
# with _GLIBCXX_USE_CXX11_ABI=0 to link cleanly.
DEFINES := \
	-DLINUX -D_LINUX -DPOSIX -DGNUC -DNDEBUG \
	-DPLATFORM_64BITS -DPLATFORM_POSIX -DCOMPILER_GCC \
	-DMATHLIB_LIB -DTIER1_LIB \
	-D_DLL_EXT=.so -D_GLIBCXX_USE_CXX11_ABI=0

CXXFLAGS ?= -O2 -g
CXXFLAGS += -m64 -std=c++11 -fpermissive -fno-strict-aliasing -fPIC \
	-msse -msse2 -mfpmath=sse -Wall -Wno-unknown-pragmas -Wno-unused-local-typedefs \
	$(DEFINES) $(INCLUDES)

# --- Sources ---------------------------------------------------------------
TOOL_SRCS := \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/navgen_app.cpp \
	$(SRC_DIR)/navgen_options.cpp \
	$(SRC_DIR)/navgen_spew.cpp
TOOL_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/tool/%.o,$(TOOL_SRCS))

# tier1 / mathlib built straight from the SDK sources. fileio.cpp is excluded
# (an int64 typedef clash under modern glibc and unused here).
TIER1_SRCS   := $(filter-out $(SDK_SRC)/tier1/fileio.cpp,$(wildcard $(SDK_SRC)/tier1/*.cpp))
MATHLIB_SRCS := $(wildcard $(SDK_SRC)/mathlib/*.cpp)
TIER1_OBJS   := $(patsubst $(SDK_SRC)/tier1/%.cpp,$(BUILD_DIR)/tier1/%.o,$(TIER1_SRCS))
MATHLIB_OBJS := $(patsubst $(SDK_SRC)/mathlib/%.cpp,$(BUILD_DIR)/mathlib/%.o,$(MATHLIB_SRCS))

# --- Link ------------------------------------------------------------------
# --start-group resolves the circular references between appframework.a and the
# tier1 archive.
LDFLAGS  += -m64 -L$(SDK_LIB) -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abspath $(SDK_LIB))
LDGROUP  := -Wl,--start-group $(SDK_LIB)/appframework.a $(BUILD_DIR)/tier1.a $(BUILD_DIR)/mathlib.a -Wl,--end-group
LDLIBS   += -ltier0 -lvstdlib -ldl -lpthread

# --- Rules -----------------------------------------------------------------
.PHONY: all clean check-sdk
all: check-sdk $(TARGET)

$(TARGET): $(TOOL_OBJS) $(BUILD_DIR)/tier1.a $(BUILD_DIR)/mathlib.a
	$(CXX) $(LDFLAGS) $(TOOL_OBJS) $(LDGROUP) $(LDLIBS) -o $@
	@echo "built $@"

$(BUILD_DIR)/tier1.a: $(TIER1_OBJS)
	$(AR) rcs $@ $(TIER1_OBJS)

$(BUILD_DIR)/mathlib.a: $(MATHLIB_OBJS)
	$(AR) rcs $@ $(MATHLIB_OBJS)

$(BUILD_DIR)/tool/%.o: $(SRC_DIR)/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tier1/%.o: $(SDK_SRC)/tier1/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/mathlib/%.o: $(SDK_SRC)/mathlib/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: dirs
dirs:
	@mkdir -p $(BUILD_DIR)/tool $(BUILD_DIR)/tier1 $(BUILD_DIR)/mathlib

check-sdk:
	@test -d "$(SDK_SRC)/public" || { \
		echo "error: Source SDK not found at $(SDK_SRC)."; \
		echo "       Run: git submodule update --init --recursive"; exit 1; }
	@test -f "$(SDK_LIB)/libtier0.so" || { \
		echo "error: $(SDK_LIB)/libtier0.so missing (incomplete submodule checkout)."; exit 1; }

clean:
	rm -rf $(BUILD_DIR)
