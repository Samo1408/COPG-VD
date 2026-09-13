cmake_minimum_required(VERSION 3.28)
project(VirtualModule)

include(FetchContent)

# Use source archives instead of Git clones. LSPlant contains test/documentation
# submodules that are not required by this module; a Git clone tries to fetch
# private-key based git@github.com URLs and breaks CI.
FetchContent_Declare(
    lsplant
    URL https://codeload.github.com/LSPosed/LSPlant/tar.gz/refs/heads/master
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    lsplant_dex_builder
    URL https://codeload.github.com/LSPosed/DexBuilder/tar.gz/refs/heads/master
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Pin Dobby to a known Android/NDK-compatible revision. Newer master revisions
# currently regress on Android clang (OSMemory include cycle + Mach-O @PAGE assembly).
FetchContent_Declare(
    dobby
    URL https://codeload.github.com/jmpews/Dobby/tar.gz/809f8ca
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Configure the dependency projects before they are added.
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(LSPLANT_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# LSPlant's CMake expects DexBuilder at this exact path. Populate it from an
# HTTPS archive before LSPlant's own add_subdirectory() is evaluated.
FetchContent_Populate(lsplant)
FetchContent_Populate(lsplant_dex_builder)
set(LSPLANT_DEX_DIR "${lsplant_SOURCE_DIR}/lsplant/src/main/jni/external/dex_builder")
file(MAKE_DIRECTORY "${LSPLANT_DEX_DIR}")
file(COPY "${lsplant_dex_builder_SOURCE_DIR}/" DESTINATION "${LSPLANT_DEX_DIR}")
FetchContent_MakeAvailable(lsplant)

# Dobby 809f8ca has two Android/Clang source issues that prevent it from
# compiling with current NDKs. Patch the vendored source before configuring it.
FetchContent_Populate(dobby)
set(DOBBY_OS_ARCH "${dobby_SOURCE_DIR}/common/os_arch_features.h")
file(READ "${DOBBY_OS_ARCH}" DOBBY_OS_TEXT)
string(REPLACE "#include \"PlatformUnifiedInterface/platform.h\"" "#include <sys/mman.h>\n#include <unistd.h>" DOBBY_OS_TEXT "${DOBBY_OS_TEXT}")
string(REGEX REPLACE "#if defined\\(ANDROID\\)\\n  auto page = \\(void \\*\\)ALIGN_FLOOR\\(address, OSMemory::PageSize\\(\\)\\);\\n  if \\(!OSMemory::SetPermission\\(page, OSMemory::PageSize\\(\\), kReadExecute\\)\\) \\{\\n    return;\\n  \\}" "#if defined(ANDROID)\n  const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);\n  const uintptr_t page_addr = ((uintptr_t)address) & ~(page_size - 1);\n  mprotect((void*)page_addr, size, PROT_READ | PROT_EXEC);" DOBBY_OS_TEXT "${DOBBY_OS_TEXT}")
file(WRITE "${DOBBY_OS_ARCH}" "${DOBBY_OS_TEXT}")

set(DOBBY_ARM64_ASM "${dobby_SOURCE_DIR}/source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm")
file(READ "${DOBBY_ARM64_ASM}" DOBBY_ARM64_TEXT)
string(REPLACE "cdecl(common_closure_bridge_handler)@PAGE" ":pg_hi21:cdecl(common_closure_bridge_handler)" DOBBY_ARM64_TEXT "${DOBBY_ARM64_TEXT}")
string(REPLACE "cdecl(common_closure_bridge_handler)@PAGEOFF" ":lo12:cdecl(common_closure_bridge_handler)" DOBBY_ARM64_TEXT "${DOBBY_ARM64_TEXT}")
file(WRITE "${DOBBY_ARM64_ASM}" "${DOBBY_ARM64_TEXT}")

add_subdirectory("${dobby_SOURCE_DIR}" "${dobby_BINARY_DIR}" EXCLUDE_FROM_ALL)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the hook backends into the module; do not ship a second shared Dobby library.
add_compile_options(-Os -flto -ffunction-sections -fdata-sections)
add_link_options(-flto -Wl,--gc-sections)


if(NOT DEBUG)
    add_compile_options(-g0 -DNDEBUG)
    add_link_options(-s)
endif()

set(ZYGISK_SOURCES
    virtual_module.cpp
    atexit.cpp
    lsplant_runtime.cpp
    device_hooks.cpp
)

add_library(virtual SHARED ${ZYGISK_SOURCES})

find_library(log-lib log)

include_directories(${CMAKE_SOURCE_DIR})
# LSPlant public headers are exposed by its CMake target.

target_link_libraries(virtual ${log-lib} lsplant_static dobby)

set_target_properties(virtual PROPERTIES
    LINK_FLAGS "-Wl,--exclude-libs,ALL"
)
