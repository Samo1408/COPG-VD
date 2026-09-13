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
FetchContent_Populate(dobby)

# Dobby 809f8ca has Android/NDK compatibility issues. Keep the dependency pinned and
# patch it deterministically at configure time. Every expected source is validated before
# modification so a future dependency drift fails loudly instead of producing a half-patched build.
set(DOBBY_ROOT "${dobby_SOURCE_DIR}")
set(DOBBY_CPU_HEADER "${DOBBY_ROOT}/core/arch/Cpu.h")
if(NOT EXISTS "${DOBBY_ROOT}/core/arch/CpuRegister.h")
    message(FATAL_ERROR "Dobby compatibility check failed: core/arch/CpuRegister.h is missing")
endif()
if(NOT EXISTS "${DOBBY_ROOT}/core/arch/Cpu.h")
    file(WRITE "${DOBBY_CPU_HEADER}" "#pragma once\n#include \"CpuRegister.h\"\n")
endif()

# The 809f8ca Android compiler expects MemRange::start() and RuntimeModule::base;
# older Dobby call sites use the removed field spellings. Patch only the known call-site
# tokens and fail if the problematic old RuntimeModule API remains in Dobby sources.
file(GLOB_RECURSE DOBBY_CPP_FILES CONFIGURE_DEPENDS
    "${DOBBY_ROOT}/*.h" "${DOBBY_ROOT}/*.hpp" "${DOBBY_ROOT}/*.c" "${DOBBY_ROOT}/*.cc" "${DOBBY_ROOT}/*.cpp")
foreach(_f IN LISTS DOBBY_CPP_FILES)
    file(READ "${_f}" _t)
    set(_old "${_t}")
    # Patch only field-style uses. Do not touch already-correct calls such as start().
    string(REGEX REPLACE "\\.load_address([;,)\\]])" ".base\1" _t "${_t}")
    string(REGEX REPLACE "->load_address([;,)\\]])" "->base\1" _t "${_t}")
    string(REGEX REPLACE "\\.start([;,)\\]])" ".start()\1" _t "${_t}")
    string(REGEX REPLACE "->start([;,)\\]])" "->start()\1" _t "${_t}")
    if(NOT "${_t}" STREQUAL "${_old}")
        file(WRITE "${_f}" "${_t}")
    endif()
endforeach()
set(LSPLANT_DEX_DIR "${lsplant_SOURCE_DIR}/lsplant/src/main/jni/external/dex_builder")
file(MAKE_DIRECTORY "${LSPLANT_DEX_DIR}")
file(COPY "${lsplant_dex_builder_SOURCE_DIR}/" DESTINATION "${LSPLANT_DEX_DIR}")
FetchContent_MakeAvailable(lsplant)

# Dobby 809f8ca needs two small Android/Clang compatibility patches.
# Patch the exact upstream source rather than relying on a version-sensitive
# regex, because os_arch_features.h and the ARM64 assembly are fetched at
# configure time.
set(DOBBY_OS_ARCH "${dobby_SOURCE_DIR}/common/os_arch_features.h")
if(NOT EXISTS "${DOBBY_OS_ARCH}")
    message(FATAL_ERROR "Dobby compatibility check failed: common/os_arch_features.h is missing")
endif()
file(READ "${DOBBY_OS_ARCH}" DOBBY_OS_TEXT)

# Avoid Dobby's OSMemory dependency on Android.  The old helper is only used
# to make a page executable/readable after relocation, which can be done with
# the Android/Bionic mprotect API directly.
set(DOBBY_OS_OLD
"namespace android {\ninline void make_memory_readable(void *address, size_t size) {\n#if defined(ANDROID)\n  auto page = (void *)ALIGN_FLOOR(address, OSMemory::PageSize());\n  if (!OSMemory::SetPermission(page, OSMemory::PageSize(), kReadExecute)) {\n    return;\n  }\n#endif\n}\n} // namespace android")
set(DOBBY_OS_NEW
"namespace android {\ninline void make_memory_readable(void *address, size_t size) {\n#if defined(ANDROID)\n  const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);\n  const uintptr_t page = ((uintptr_t)address) & ~(page_size - 1);\n  const size_t offset = (size_t)((uintptr_t)address - page);\n  const size_t length = (size + offset + page_size - 1) & ~(page_size - 1);\n  if (mprotect((void *)page, length, PROT_READ | PROT_EXEC) != 0) { return; }\n#endif\n}\n} // namespace android")
string(REPLACE "#include <sys/types.h>" "#include <sys/types.h>\n#include <sys/mman.h>\n#include <unistd.h>" DOBBY_OS_TEXT "${DOBBY_OS_TEXT}")
if(DOBBY_OS_TEXT MATCHES "OSMemory::PageSize\(\)" OR DOBBY_OS_TEXT MATCHES "OSMemory::SetPermission")
    string(REPLACE "${DOBBY_OS_OLD}" "${DOBBY_OS_NEW}" DOBBY_OS_TEXT "${DOBBY_OS_TEXT}")
endif()
if(DOBBY_OS_TEXT MATCHES "OSMemory::PageSize\(\)" OR DOBBY_OS_TEXT MATCHES "OSMemory::SetPermission")
    message(FATAL_ERROR "Dobby OSMemory patch did not apply")
endif()
file(WRITE "${DOBBY_OS_ARCH}" "${DOBBY_OS_TEXT}")

# Android/ELF uses ELF relocations, not Mach-O @PAGE/@PAGEOFF.
set(DOBBY_ARM64_ASM "${dobby_SOURCE_DIR}/source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm")
if(NOT EXISTS "${DOBBY_ARM64_ASM}")
    message(FATAL_ERROR "Dobby compatibility check failed: ARM64 trampoline assembly is missing")
endif()
file(READ "${DOBBY_ARM64_ASM}" DOBBY_ARM64_TEXT)
string(REPLACE "adrp TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGE"
               "adrp TMP_REG_0, cdecl(common_closure_bridge_handler)"
               DOBBY_ARM64_TEXT "${DOBBY_ARM64_TEXT}")
string(REPLACE "add TMP_REG_0, TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGEOFF"
               "add TMP_REG_0, TMP_REG_0, :lo12:cdecl(common_closure_bridge_handler)"
               DOBBY_ARM64_TEXT "${DOBBY_ARM64_TEXT}")
if(DOBBY_ARM64_TEXT MATCHES "@PAGE" OR DOBBY_ARM64_TEXT MATCHES "@PAGEOFF")
    message(FATAL_ERROR "Dobby ARM64 relocation patch did not apply")
endif()
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

# Link the static hook backend so no separate libdobby.so is required at runtime.
# The packaging workflow expects the Zygisk binary under this stable output name.
set_target_properties(virtual PROPERTIES OUTPUT_NAME spoof)

find_library(log-lib log)

include_directories(${CMAKE_SOURCE_DIR})
# LSPlant public headers are exposed by its CMake target.

target_link_libraries(virtual ${log-lib} lsplant_static dobby_static)

set_target_properties(virtual PROPERTIES
    LINK_FLAGS "-Wl,--exclude-libs,ALL"
)
