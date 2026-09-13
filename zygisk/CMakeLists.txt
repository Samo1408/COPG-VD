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
FetchContent_Declare(
    dobby
    URL https://codeload.github.com/jmpews/Dobby/tar.gz/refs/heads/master
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
FetchContent_MakeAvailable(lsplant dobby)

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
