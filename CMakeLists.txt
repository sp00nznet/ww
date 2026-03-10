cmake_minimum_required(VERSION 3.20)
project(WindWakerRecomp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)
endif()

# Build options
option(WW_BUILD_RECOMPILER "Build the static recompiler tool" ON)
option(WW_BUILD_RUNTIME "Build the game runtime" ON)
option(WW_BUILD_TOOLS "Build auxiliary tools (DOL parser, asset extractor)" ON)

# ===========================================================================
# gcrecomp — Generic GameCube recompilation toolkit (submodule)
# Provides: gcrecomp_gx, gcrecomp_audio, gcrecomp_input, gcrecomp_runtime
# ===========================================================================
add_subdirectory(lib/gcrecomp)

# ===========================================================================
# Tool: DOL/REL Parser & Static Recompiler
# ===========================================================================
if(WW_BUILD_RECOMPILER)
    add_executable(ww_recompiler
        tools/recompiler/main.cpp
        tools/recompiler/dol_parser.cpp
        tools/recompiler/rel_parser.cpp
        tools/recompiler/ppc_disasm.cpp
        tools/recompiler/ppc_to_c.cpp
        tools/recompiler/cfg_builder.cpp
        tools/recompiler/symbol_map.cpp
    )
    target_include_directories(ww_recompiler PRIVATE include)
    target_compile_definitions(ww_recompiler PRIVATE WW_TOOL)
endif()

# ===========================================================================
# Runtime Library: Wind Waker-specific OS/runtime layer
#
# Game-specific OS HLE, memory map, and function table. Links against
# gcrecomp for GX graphics, audio, and input.
# ===========================================================================
if(WW_BUILD_RUNTIME)
    add_library(ww_runtime STATIC
        src/runtime/ppc_context.cpp
        src/runtime/memory.cpp
        src/runtime/func_table.cpp
        src/runtime/os_funcs.cpp
    )
    target_include_directories(ww_runtime PUBLIC include)
    target_link_libraries(ww_runtime PUBLIC gcrecomp_runtime)

    # Main executable (links recompiled code + runtime + gcrecomp libraries)
    add_executable(ww_launcher
        src/main.cpp
    )
    target_link_libraries(ww_launcher PRIVATE
        ww_runtime
        gcrecomp_gx
        gcrecomp_audio
        gcrecomp_input
    )
    target_include_directories(ww_launcher PRIVATE include)
    if(WIN32)
        target_link_libraries(ww_launcher PRIVATE d3d11 d3dcompiler dxgi)
    endif()
endif()

# ===========================================================================
# Auxiliary Tools
# ===========================================================================
if(WW_BUILD_TOOLS)
    add_executable(ww_dol_info
        tools/dol_info/main.cpp
        tools/recompiler/dol_parser.cpp
        tools/recompiler/ppc_disasm.cpp
    )
    target_include_directories(ww_dol_info PRIVATE include)
endif()
