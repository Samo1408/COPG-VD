cmake_minimum_required(VERSION 3.28)
project(VirtualModule)

include(FetchContent)

# LSPlant provides the ART Java hook engine; Dobby supplies the inline hook backend.
# They are fetched at build time so the project ZIP stays self-contained in source form.
FetchContent_Declare(
    lsplant
    GIT_REPOSITORY https://github.com/LSPosed/LSPlant.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    dobby
    GIT_REPOSITORY https://github.com/jmpews/Dobby.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lsplant dobby)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the hook backends into the module; do not ship a second shared Dobby library.
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(LSPLANT_BUILD_SHARED OFF CACHE BOOL "" FORCE)

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
