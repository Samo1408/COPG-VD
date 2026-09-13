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

# Dobby 809f8ca has Android/NDK compatibility issues. Patch only files
# that actually exist in this pinned revision. In particular, this revision
# calls the Linux runtime file ProcessRuntimeUtility.cc (not ProcessRuntime.cc).
set(DOBBY_ROOT "${dobby_SOURCE_DIR}")
set(DOBBY_CODE_PATCH "${DOBBY_ROOT}/source/Backend/UserMode/ExecMemory/code-patch-tool-posix.cc")
set(DOBBY_PROCESS_RUNTIME "${DOBBY_ROOT}/source/Backend/UserMode/PlatformUtil/Linux/ProcessRuntimeUtility.cc")
set(DOBBY_LOGGING "${DOBBY_ROOT}/external/logging/logging.cc")

foreach(_required IN ITEMS DOBBY_CODE_PATCH DOBBY_PROCESS_RUNTIME DOBBY_LOGGING)
    if(NOT EXISTS "${${_required}}")
        message(FATAL_ERROR "Dobby compatibility check failed: required source is missing: ${${_required}}")
    endif()
endforeach()

# Android build fix: code-patch-tool-posix.cc in 809f8ca still includes the
# removed generic Cpu.h header.
file(READ "${DOBBY_CODE_PATCH}" DOBBY_CODE_PATCH_TEXT)
string(REPLACE "#include \"core/arch/Cpu.h\""
               "#include \"core/arch/CpuRegister.h\""
               DOBBY_CODE_PATCH_TEXT "${DOBBY_CODE_PATCH_TEXT}")
if(DOBBY_CODE_PATCH_TEXT MATCHES "#include \"core/arch/Cpu\\.h\"")
    message(FATAL_ERROR "Dobby Cpu.h compatibility patch did not apply")
endif()
if(NOT DOBBY_CODE_PATCH_TEXT MATCHES "#include \"core/arch/CpuRegister\\.h\"")
    message(FATAL_ERROR "Dobby CpuRegister.h include is missing after patch")
endif()
file(WRITE "${DOBBY_CODE_PATCH}" "${DOBBY_CODE_PATCH_TEXT}")

# Android/NDK compatibility: RuntimeModule exposes its load address as `base`
# in this Dobby revision. Patch only the two ELF/Linux users below.
file(READ "${DOBBY_PROCESS_RUNTIME}" DOBBY_PROCESS_TEXT)
string(REPLACE
    "static bool memory_region_comparator(MemRange a, MemRange b)"
    "static bool memory_region_comparator(const MemRange& a, const MemRange& b)"
    DOBBY_PROCESS_TEXT "${DOBBY_PROCESS_TEXT}")
string(REPLACE "return (a.start < b.start);" "return (a.start() < b.start());"
    DOBBY_PROCESS_TEXT "${DOBBY_PROCESS_TEXT}")
if(DOBBY_PROCESS_TEXT MATCHES "memory_region_comparator\\(MemRange a, MemRange b\\)")
    message(FATAL_ERROR "Dobby MemRange comparator patch did not apply")
endif()
if(DOBBY_PROCESS_TEXT MATCHES "return \\(a\\.start < b\\.start\\);")
    message(FATAL_ERROR "Dobby MemRange::start patch did not apply")
endif()
if(NOT DOBBY_PROCESS_TEXT MATCHES "return \\(a\\.start\\(\\) < b\\.start\\(\\)\\);")
    message(FATAL_ERROR "Dobby MemRange::start() form is missing after patch")
endif()
file(WRITE "${DOBBY_PROCESS_RUNTIME}" "${DOBBY_PROCESS_TEXT}")

set(DOBBY_SYMBOL_RESOLVER "${DOBBY_ROOT}/builtin-plugin/SymbolResolver/elf/dobby_symbol_resolver.cc")
if(NOT EXISTS "${DOBBY_SYMBOL_RESOLVER}")
    message(FATAL_ERROR "Dobby compatibility check failed: ELF symbol resolver is missing")
endif()
file(READ "${DOBBY_SYMBOL_RESOLVER}" DOBBY_SYMBOL_TEXT)
set(DOBBY_SYMBOL_OLD "module.load_address")
set(DOBBY_SYMBOL_NEW "module.base")
string(REPLACE "${DOBBY_SYMBOL_OLD}" "${DOBBY_SYMBOL_NEW}"
               DOBBY_SYMBOL_TEXT "${DOBBY_SYMBOL_TEXT}")
if(DOBBY_SYMBOL_TEXT MATCHES "module\\.load_address")
    message(FATAL_ERROR "Dobby RuntimeModule load_address patch did not apply in symbol resolver")
endif()
if(NOT DOBBY_SYMBOL_TEXT MATCHES "module\\.base")
    message(FATAL_ERROR "Dobby RuntimeModule::base is missing in symbol resolver after patch")
endif()
file(WRITE "${DOBBY_SYMBOL_RESOLVER}" "${DOBBY_SYMBOL_TEXT}")

# ProcessRuntimeUtility has both assignments and reads of RuntimeModule::load_address.
file(READ "${DOBBY_PROCESS_RUNTIME}" DOBBY_PROCESS_TEXT2)
string(REPLACE "module.load_address" "module.base"
               DOBBY_PROCESS_TEXT2 "${DOBBY_PROCESS_TEXT2}")
if(DOBBY_PROCESS_TEXT2 MATCHES "module\\.load_address")
    message(FATAL_ERROR "Dobby RuntimeModule load_address patch did not apply in ProcessRuntimeUtility")
endif()
if(NOT DOBBY_PROCESS_TEXT2 MATCHES "module\\.base")
    message(FATAL_ERROR "Dobby RuntimeModule::base is missing in ProcessRuntimeUtility after patch")
endif()
file(WRITE "${DOBBY_PROCESS_RUNTIME}" "${DOBBY_PROCESS_TEXT2}")

# Android/Bionic provides gettimeofday through sys/time.h on this revision.
file(READ "${DOBBY_LOGGING}" DOBBY_LOGGING_TEXT)
if(NOT DOBBY_LOGGING_TEXT MATCHES "#include <sys/time\\.h>")
    string(REPLACE "#include <sys/types.h>"
                   "#include <sys/types.h>\n#include <sys/time.h>"
                   DOBBY_LOGGING_TEXT "${DOBBY_LOGGING_TEXT}")
endif()
if(NOT DOBBY_LOGGING_TEXT MATCHES "#include <sys/time\\.h>")
    message(FATAL_ERROR "Dobby logging sys/time.h patch did not apply")
endif()
file(WRITE "${DOBBY_LOGGING}" "${DOBBY_LOGGING_TEXT}")

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

# LSPlant exposes its public header from:
# lsplant/src/main/jni/include/lsplant.hpp
# Do not rely solely on transitive include propagation here: when lsplant_static
# is consumed from a parent project, CMake/module scanning may not propagate
# the PUBLIC include directory to this target on all NDK/CMake combinations.
set(LSPLANT_PUBLIC_INCLUDE_DIR "${lsplant_SOURCE_DIR}/lsplant/src/main/jni/include")
if(NOT EXISTS "${LSPLANT_PUBLIC_INCLUDE_DIR}/lsplant.hpp")
    message(FATAL_ERROR "LSPlant public header is missing: ${LSPLANT_PUBLIC_INCLUDE_DIR}/lsplant.hpp")
endif()
target_include_directories(virtual PRIVATE "${LSPLANT_PUBLIC_INCLUDE_DIR}")

target_link_libraries(virtual ${log-lib} lsplant_static dobby_static)

set_target_properties(virtual PROPERTIES
    LINK_FLAGS "-Wl,--exclude-libs,ALL"
)
