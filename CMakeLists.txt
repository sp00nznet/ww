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
# Runtime Library: Links with recompiled game code
# ===========================================================================
if(WW_BUILD_RUNTIME)
    # Runtime core
    add_library(ww_runtime STATIC
        src/runtime/ppc_context.cpp
        src/runtime/memory.cpp
        src/runtime/func_table.cpp
        src/runtime/os_funcs.cpp
    )
    target_include_directories(ww_runtime PUBLIC include)

    # GX Graphics (D3D11 backend)
    add_library(ww_gx STATIC
        src/gx/gx_core.cpp
        src/gx/gx_tev.cpp
        src/gx/gx_texture.cpp
        src/gx/gx_vertex.cpp
        src/gx/gx_matrix.cpp
        src/gx/gx_display_list.cpp
        src/gx/d3d11/d3d11_backend.cpp
        src/gx/d3d11/d3d11_shaders.cpp
    )
    target_include_directories(ww_gx PUBLIC include)
    if(WIN32)
        target_link_libraries(ww_gx PRIVATE d3d11 d3dcompiler dxgi)
    endif()

    # Audio runtime
    add_library(ww_audio STATIC
        src/audio/dsp_decoder.cpp
        src/audio/audio_mixer.cpp
        src/audio/audio_system.cpp
    )
    target_include_directories(ww_audio PUBLIC include)
    if(WIN32)
        target_link_libraries(ww_audio PRIVATE xaudio2)
    endif()

    # Input
    add_library(ww_input STATIC
        src/input/input_system.cpp
        src/input/pad_mapping.cpp
    )
    target_include_directories(ww_input PUBLIC include)
    if(WIN32)
        target_link_libraries(ww_input PRIVATE xinput)
    endif()

    # Main executable (links recompiled code + runtime)
    # This target is configured after recompilation generates the source files
    # For now, we build a launcher/test harness
    add_executable(ww_launcher
        src/main.cpp
    )
    target_link_libraries(ww_launcher PRIVATE ww_runtime ww_gx ww_audio ww_input)
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
