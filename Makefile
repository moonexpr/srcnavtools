# ===========================================================================
#  navtools_create_mesh - Makefile (Linux / gcc)
#
#  Direct (non-container) build. Two architectures:
#
#    make                 # 64-bit (default): vendored 64-bit SDK fork
#                         #   (external/source-sdk-2013). Compiles tier1/mathlib
#                         #   from source, links prebuilt libtier0/libvstdlib/
#                         #   appframework.a. Fully builds + links here.
#                         #   -> build/navtools_create_mesh
#
#    make ARCH=32           # 32-bit: classic SDK (external/source-sdk-2013-classic,
#                         #   "singleplayer" branch). Links the classic 32-bit
#                         #   tier0/vstdlib/tier1/mathlib. Needs g++-multilib.
#                         #   -> build/navtools_create_mesh32
#
#  Both arches are self-contained: CAppSystemGroup is reimplemented in
#  appframework_min.cpp (the SDKs ship no Linux appframework source), so no
#  prebuilt appframework.a is needed. The 32-bit build also provides g_pMemAlloc
#  via navgen_memalloc.cpp, because the classic Linux libtier0.so does not export
#  it (the 64-bit libtier0.so does). See docs/ARCHITECTURE.md.
#
#  Canonical Steam-Runtime build instead: scripts/build_steamrt.sh.
# ===========================================================================

ARCH      ?= 64
CXX       ?= g++
BUILD_DIR := build
SRC_DIR   := src/navtools_create_mesh

ifeq ($(ARCH),32)
  SDK        ?= external/source-sdk-2013-classic
  SDK_SRC    := $(SDK)/src
  SDK_LIB    ?= $(SDK_SRC)/lib/public/linux32
  ARCHFLAGS  := -m32
  ARCHDEFS   := -DNAVTOOLS_PROVIDE_MEMALLOC
  TARGET     := $(BUILD_DIR)/navtools_create_mesh32
  # classic SDK ships tier1/mathlib prebuilt as 32-bit archives. We provide
  # CAppSystemGroup (appframework_min.cpp) and g_pMemAlloc (navgen_memalloc.cpp)
  # ourselves, since the classic SDK ships neither on Linux.
  STATIC_LIBS := $(SDK_LIB)/tier1.a $(SDK_LIB)/mathlib.a
  ARCH_EXTRA_SRCS := $(SRC_DIR)/navgen_memalloc.cpp $(SRC_DIR)/navgen_compat.cpp
  BUILD_TIER  := 0
else
  SDK        ?= external/source-sdk-2013
  SDK_SRC    := $(SDK)/src
  SDK_LIB    ?= $(SDK_SRC)/lib/public/linux64
  ARCHFLAGS  := -m64
  ARCHDEFS   := -DPLATFORM_64BITS
  TARGET     := $(BUILD_DIR)/navtools_create_mesh
  # tier1/mathlib built from source; CAppSystemGroup provided by
  # appframework_min.cpp (g_pMemAlloc comes from the 64-bit libtier0.so).
  STATIC_LIBS := $(BUILD_DIR)/tier1.a $(BUILD_DIR)/mathlib.a
  ARCH_EXTRA_SRCS :=
  BUILD_TIER  := 1
endif

INCLUDES := \
	-I$(SRC_DIR) \
	-I$(SDK_SRC)/public \
	-I$(SDK_SRC)/public/tier0 \
	-I$(SDK_SRC)/public/tier1 \
	-I$(SDK_SRC)/public/mathlib \
	-I$(SDK_SRC)/common \
	-I$(SDK_SRC)/mathlib

# The prebuilt libtier0/libvstdlib use the pre-C++11 libstdc++ ABI.
DEFINES := \
	-DLINUX -D_LINUX -DPOSIX -DGNUC -DNDEBUG -DPLATFORM_POSIX -DCOMPILER_GCC \
	-DMATHLIB_LIB -DTIER1_LIB -D_DLL_EXT=.so -D_GLIBCXX_USE_CXX11_ABI=0 $(ARCHDEFS)

CXXFLAGS ?= -O2 -g
CXXFLAGS += $(ARCHFLAGS) -std=c++11 -fpermissive -fno-strict-aliasing -fPIC \
	-msse -msse2 -mfpmath=sse -Wall -Wno-unknown-pragmas -Wno-unused-local-typedefs \
	$(DEFINES) $(INCLUDES)

OBJDIR := $(BUILD_DIR)/tool$(ARCH)
TOOL_SRCS := $(SRC_DIR)/main.cpp $(SRC_DIR)/navgen_app.cpp \
             $(SRC_DIR)/navgen_options.cpp $(SRC_DIR)/navgen_spew.cpp \
             $(SRC_DIR)/appframework_min.cpp $(ARCH_EXTRA_SRCS)
TOOL_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJDIR)/%.o,$(TOOL_SRCS))

TIER1_SRCS   := $(filter-out $(SDK_SRC)/tier1/fileio.cpp,$(wildcard $(SDK_SRC)/tier1/*.cpp))
MATHLIB_SRCS := $(wildcard $(SDK_SRC)/mathlib/*.cpp)
TIER1_OBJS   := $(patsubst $(SDK_SRC)/tier1/%.cpp,$(BUILD_DIR)/tier1/%.o,$(TIER1_SRCS))
MATHLIB_OBJS := $(patsubst $(SDK_SRC)/mathlib/%.cpp,$(BUILD_DIR)/mathlib/%.o,$(MATHLIB_SRCS))

LDFLAGS  += $(ARCHFLAGS) -L$(SDK_LIB) -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abspath $(SDK_LIB))
LDGROUP  := -Wl,--start-group $(STATIC_LIBS) -Wl,--end-group
LDLIBS   += -ltier0 -lvstdlib $(EXTRA_LDLIBS) -ldl -lpthread

.PHONY: all clean check-sdk dirs
all: check-sdk $(TARGET)

ifeq ($(BUILD_TIER),1)
$(TARGET): $(TOOL_OBJS) $(BUILD_DIR)/tier1.a $(BUILD_DIR)/mathlib.a
	$(CXX) $(LDFLAGS) $(TOOL_OBJS) $(LDGROUP) $(LDLIBS) -o $@
	@echo "built $@ (ARCH=$(ARCH))"
else
$(TARGET): $(TOOL_OBJS)
	$(CXX) $(LDFLAGS) $(TOOL_OBJS) $(LDGROUP) $(LDLIBS) -o $@
	@echo "built $@ (ARCH=$(ARCH))"
endif

$(BUILD_DIR)/tier1.a: $(TIER1_OBJS)
	$(AR) rcs $@ $(TIER1_OBJS)
$(BUILD_DIR)/mathlib.a: $(MATHLIB_OBJS)
	$(AR) rcs $@ $(MATHLIB_OBJS)

$(OBJDIR)/%.o: $(SRC_DIR)/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/tier1/%.o: $(SDK_SRC)/tier1/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/mathlib/%.o: $(SDK_SRC)/mathlib/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

dirs:
	@mkdir -p $(OBJDIR) $(BUILD_DIR)/tier1 $(BUILD_DIR)/mathlib

check-sdk:
	@test -d "$(SDK_SRC)/public" || { \
		echo "error: SDK not found at $(SDK_SRC). Run: git submodule update --init --recursive"; exit 1; }
	@test -f "$(SDK_LIB)/libtier0.so" || { \
		echo "error: $(SDK_LIB)/libtier0.so missing (incomplete submodule checkout)."; exit 1; }

clean:
	rm -rf $(BUILD_DIR)
