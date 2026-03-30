cmake_minimum_required(VERSION 3.18)

# Vulkan native viewport application (replaces UE adapter).

add_executable(genesis_runtime
    src/main.cpp
    src/GE_app.cpp
    src/camera_controller.cpp
    src/ai_ui.cpp
    src/openxr_runtime.cpp
    src/obj_import.cpp
    src/scene.cpp
    src/scene_api.cpp
)

add_executable(genesis_editor
    src/main.cpp
    src/GE_app.cpp
    src/camera_controller.cpp
    src/ai_ui.cpp
    src/openxr_runtime.cpp
    src/obj_import.cpp
    src/scene.cpp
    src/scene_api.cpp
)

if (WIN32)
    # main.cpp provides wWinMain entry point.
    set_target_properties(genesis_runtime PROPERTIES WIN32_EXECUTABLE TRUE)
    set_target_properties(genesis_editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(genesis_runtime PRIVATE include)
target_link_libraries(genesis_runtime PRIVATE EigenWareCore)
target_compile_definitions(genesis_runtime PRIVATE GENESIS_EDITOR_BUILD=0)

target_include_directories(genesis_editor PRIVATE include)
target_link_libraries(genesis_editor PRIVATE EigenWareCore)
target_compile_definitions(genesis_editor PRIVATE GENESIS_EDITOR_BUILD=1)

# -----------------------------------------------------------------------------
# OpenXR (optional, production path)
#
# We integrate OpenXR via the Vulkan graphics binding. The build remains
# deterministic and functional without OpenXR; when headers/loader are found
# we enable XR and expose eye-gaze (XR_EXT_eye_gaze_interaction) if available.
#
# NOTE: We do NOT vendor the OpenXR SDK. Users install OpenXR SDK/runtime.
# -----------------------------------------------------------------------------

find_path(EW_OPENXR_INCLUDE_DIR
    NAMES openxr/openxr.h
    HINTS ENV OPENXR_SDK ENV OpenXR_SDK
    PATH_SUFFIXES include Include)

find_library(EW_OPENXR_LOADER_LIB
    NAMES openxr_loader
    HINTS ENV OPENXR_SDK ENV OpenXR_SDK
    PATH_SUFFIXES lib Lib)

if (EW_OPENXR_INCLUDE_DIR AND EW_OPENXR_LOADER_LIB)
    message(STATUS "OpenXR found: enabling Vulkan OpenXR path")
    target_include_directories(genesis_runtime PRIVATE "${EW_OPENXR_INCLUDE_DIR}")
    target_link_libraries(genesis_runtime PRIVATE "${EW_OPENXR_LOADER_LIB}")
    target_compile_definitions(genesis_runtime PRIVATE GENESIS_HAS_OPENXR=1)

    target_include_directories(genesis_editor PRIVATE "${EW_OPENXR_INCLUDE_DIR}")
    target_link_libraries(genesis_editor PRIVATE "${EW_OPENXR_LOADER_LIB}")
    target_compile_definitions(genesis_editor PRIVATE GENESIS_HAS_OPENXR=1)
else()
    message(STATUS "OpenXR not found: building without XR (desktop viewport only)")
endif()

if (WIN32)
    target_compile_definitions(genesis_runtime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(genesis_editor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # Vulkan loader is provided by the Vulkan SDK.
    find_package(Vulkan REQUIRED)
    target_link_libraries(genesis_runtime PRIVATE Vulkan::Vulkan user32 gdi32 shell32 comdlg32)
    target_link_libraries(genesis_editor PRIVATE Vulkan::Vulkan user32 gdi32 shell32 comdlg32)
else()
    find_package(Vulkan REQUIRED)
    target_link_libraries(genesis_runtime PRIVATE Vulkan::Vulkan)
    target_link_libraries(genesis_editor PRIVATE Vulkan::Vulkan)
endif()

# -----------------------------------------------------------------------------
# Shaders (compiled at build time via Vulkan SDK tools)
# -----------------------------------------------------------------------------
# We intentionally compile GLSL -> SPIR-V during the build so the runtime stays
# deterministic and does not depend on runtime shader compilation libraries.

set(EW_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(EW_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(EW_RESEARCH_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/vkcompute")
file(MAKE_DIRECTORY "${EW_SHADER_OUT}")

if (WIN32)
    # Require glslc (Vulkan SDK).
    find_program(EW_GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)
    find_program(EW_GLSLANG glslangValidator HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)

    if (EW_GLSLC)
        set(EW_SHADER_COMPILER "${EW_GLSLC}")
        set(EW_COMP_MODE "glslc")
    elseif(EW_GLSLANG)
        set(EW_SHADER_COMPILER "${EW_GLSLANG}")
        set(EW_COMP_MODE "glslang")
    else()
        message(FATAL_ERROR "Shader compiler not found. Install Vulkan SDK (glslc or glslangValidator).")
    endif()
endif()

set(EW_SHADERS
    "${EW_SHADER_DIR}/instanced.vert"
    "${EW_SHADER_DIR}/instanced.frag"
    "${EW_SHADER_DIR}/cam_hist.comp"
    "${EW_SHADER_DIR}/cam_median.comp"
)

set(EW_SPV
    "${EW_SHADER_OUT}/instanced.vert.spv"
    "${EW_SHADER_OUT}/instanced.frag.spv"
    "${EW_SHADER_OUT}/cam_hist.comp.spv"
    "${EW_SHADER_OUT}/cam_median.comp.spv"
    "${EW_SHADER_OUT}/vkcompute_photon_visual.comp.spv"
    "${EW_SHADER_OUT}/vkcompute_photon_audio.comp.spv"
    "${EW_SHADER_OUT}/vkcompute_stov_visual.comp.spv"
)

if (WIN32)
    function(ew_add_shader_compile_rule shader_src shader_out)
        if (EW_COMP_MODE STREQUAL "glslc")
            add_custom_command(
                OUTPUT "${shader_out}"
                COMMAND "${EW_SHADER_COMPILER}" -o "${shader_out}" "${shader_src}"
                DEPENDS "${shader_src}"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT "${shader_out}"
                COMMAND "${EW_SHADER_COMPILER}" -V "${shader_src}" -o "${shader_out}"
                DEPENDS "${shader_src}"
                VERBATIM)
        endif()
    endfunction()

    function(ew_add_compute_shader_compile_rule shader_src shader_out)
        if (EW_COMP_MODE STREQUAL "glslc")
            add_custom_command(
                OUTPUT "${shader_out}"
                COMMAND "${EW_SHADER_COMPILER}" -fshader-stage=comp -o "${shader_out}" "${shader_src}"
                DEPENDS "${shader_src}"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT "${shader_out}"
                COMMAND "${EW_SHADER_COMPILER}" -V -S comp "${shader_src}" -o "${shader_out}"
                DEPENDS "${shader_src}"
                VERBATIM)
        endif()
    endfunction()

    ew_add_shader_compile_rule("${EW_SHADER_DIR}/instanced.vert" "${EW_SHADER_OUT}/instanced.vert.spv")
    ew_add_shader_compile_rule("${EW_SHADER_DIR}/instanced.frag" "${EW_SHADER_OUT}/instanced.frag.spv")
    ew_add_shader_compile_rule("${EW_SHADER_DIR}/cam_hist.comp" "${EW_SHADER_OUT}/cam_hist.comp.spv")
    ew_add_shader_compile_rule("${EW_SHADER_DIR}/cam_median.comp" "${EW_SHADER_OUT}/cam_median.comp.spv")
    ew_add_compute_shader_compile_rule("${EW_RESEARCH_SHADER_DIR}/vkcompute_photon_visual.glsl" "${EW_SHADER_OUT}/vkcompute_photon_visual.comp.spv")
    ew_add_compute_shader_compile_rule("${EW_RESEARCH_SHADER_DIR}/vkcompute_photon_audio.glsl" "${EW_SHADER_OUT}/vkcompute_photon_audio.comp.spv")
    ew_add_compute_shader_compile_rule("${EW_RESEARCH_SHADER_DIR}/vkcompute_stov_visual.comp" "${EW_SHADER_OUT}/vkcompute_stov_visual.comp.spv")

    add_custom_target(EWShaders ALL DEPENDS ${EW_SPV})
    add_dependencies(genesis_runtime EWShaders)
    add_dependencies(genesis_editor EWShaders)
    target_compile_definitions(genesis_runtime PRIVATE EW_SHADER_OUT_DIR="${EW_SHADER_OUT}")
    target_compile_definitions(genesis_editor PRIVATE EW_SHADER_OUT_DIR="${EW_SHADER_OUT}")
endif()

set_target_properties(genesis_runtime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/GenesisEngineState/Binaries/Win64"
)
set_target_properties(genesis_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/GenesisEngineState/Binaries/Win64"
)

# Hard fail if someone configures a 32-bit Windows build.
if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "GenesisEngine Vulkan apps must be built as Win64 (x64). Configure with -A x64.")
    endif()
endif()
