cmake_minimum_required(VERSION 3.18)

# Vulkan native viewport application (replaces UE adapter).

include("${CMAKE_SOURCE_DIR}/cmake/EwSpirvTools.cmake")

set(GENESIS_APP_SOURCES
    src/main.cpp
    src/GE_app.cpp
    src/camera_controller.cpp
    src/ai_ui.cpp
    src/ew_openai_chat.cpp
    src/openxr_runtime.cpp
    src/obj_import.cpp
    src/scene.cpp
    src/scene_api.cpp
)

if (WIN32)
    list(APPEND GENESIS_APP_SOURCES resources/genesis_icon.rc)
endif()

add_executable(genesis_app ${GENESIS_APP_SOURCES})
target_include_directories(genesis_app PRIVATE include)
target_link_libraries(genesis_app PRIVATE EigenWareCore)
target_compile_definitions(genesis_app PRIVATE GENESIS_EDITOR_BUILD=1)

option(GENESIS_BUILD_RUNTIME_APP "Build shipping runtime app without editor tools" ON)
if (GENESIS_BUILD_RUNTIME_APP)
    add_executable(genesis_runtime ${GENESIS_APP_SOURCES})
    target_include_directories(genesis_runtime PRIVATE include)
    target_link_libraries(genesis_runtime PRIVATE EigenWareCore)
    target_compile_definitions(genesis_runtime PRIVATE GENESIS_EDITOR_BUILD=0 GENESIS_RUNTIME_BUILD=1)
endif()

if (WIN32)
    # Launch as a normal Windows application when started from Explorer or an installer.
    target_link_options(genesis_app PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    if (TARGET genesis_runtime)
        target_link_options(genesis_runtime PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
endif()

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
    target_include_directories(genesis_app PRIVATE "${EW_OPENXR_INCLUDE_DIR}")
    target_link_libraries(genesis_app PRIVATE "${EW_OPENXR_LOADER_LIB}")
    target_compile_definitions(genesis_app PRIVATE GENESIS_HAS_OPENXR=1)
    if (TARGET genesis_runtime)
        target_include_directories(genesis_runtime PRIVATE "${EW_OPENXR_INCLUDE_DIR}")
        target_link_libraries(genesis_runtime PRIVATE "${EW_OPENXR_LOADER_LIB}")
        target_compile_definitions(genesis_runtime PRIVATE GENESIS_HAS_OPENXR=1)
    endif()
else()
    message(STATUS "OpenXR not found: building without XR (desktop viewport only)")
endif()

if (WIN32)
    target_compile_definitions(genesis_app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if (TARGET genesis_runtime)
        target_compile_definitions(genesis_runtime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    # Vulkan loader is provided by the Vulkan SDK.
    find_package(Vulkan REQUIRED)
    target_link_libraries(genesis_app PRIVATE Vulkan::Vulkan user32 gdi32 shell32 comdlg32 winhttp)
    if (TARGET genesis_runtime)
        target_link_libraries(genesis_runtime PRIVATE Vulkan::Vulkan user32 gdi32 shell32 comdlg32 winhttp)
    endif()
else()
    find_package(Vulkan REQUIRED)
    target_link_libraries(genesis_app PRIVATE Vulkan::Vulkan)
    if (TARGET genesis_runtime)
        target_link_libraries(genesis_runtime PRIVATE Vulkan::Vulkan)
    endif()
endif()

# -----------------------------------------------------------------------------
# Shaders (compiled at build time via Vulkan SDK tools)
# -----------------------------------------------------------------------------
# We intentionally compile GLSL -> SPIR-V during the build so the runtime stays
# deterministic and does not depend on runtime shader compilation libraries.

set(EW_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(EW_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${EW_SHADER_OUT}")

if (WIN32)
    ew_find_spirv_compiler(EW_SHADER_COMPILER EW_COMP_MODE)
endif()

set(EW_SHADERS
    "${EW_SHADER_DIR}/instanced.vert"
    "${EW_SHADER_DIR}/instanced.frag"
)

set(EW_SPV
    "${EW_SHADER_OUT}/instanced.vert.spv"
    "${EW_SHADER_OUT}/instanced.frag.spv"
)

if (WIN32)
    ew_add_spirv_shader_command(
        "${EW_SHADER_COMPILER}"
        "${EW_COMP_MODE}"
        "${EW_SHADER_DIR}/instanced.vert"
        "${EW_SHADER_OUT}/instanced.vert.spv")

    ew_add_spirv_shader_command(
        "${EW_SHADER_COMPILER}"
        "${EW_COMP_MODE}"
        "${EW_SHADER_DIR}/instanced.frag"
        "${EW_SHADER_OUT}/instanced.frag.spv")

    add_custom_target(EWShaders ALL DEPENDS ${EW_SPV})
    add_dependencies(genesis_app EWShaders)
    if (TARGET genesis_runtime)
        add_dependencies(genesis_runtime EWShaders)
    endif()
    target_compile_definitions(genesis_app PRIVATE EW_SHADER_OUT_DIR="${EW_SHADER_OUT}")
    if (TARGET genesis_runtime)
        target_compile_definitions(genesis_runtime PRIVATE EW_SHADER_OUT_DIR="${EW_SHADER_OUT}")
    endif()
endif()

set_target_properties(genesis_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/GenesisEngineState/Binaries/Win64/Editor"
)
if (TARGET genesis_runtime)
    set_target_properties(genesis_runtime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/GenesisEngineState/Binaries/Win64/Runtime"
    )
endif()

set(GENESIS_RUNTIME_EDITOR_SPLIT_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/GENESIS_RUNTIME_EDITOR_SPLIT_MANIFEST.txt")
file(GENERATE OUTPUT "${GENESIS_RUNTIME_EDITOR_SPLIT_MANIFEST}" CONTENT
"Genesis Engine Runtime/Editor Split\n\nRuntime target: genesis_runtime\nRuntime output: ${CMAKE_BINARY_DIR}/GenesisEngineState/Binaries/Win64/Runtime\nEditor target: genesis_app\nEditor output: ${CMAKE_BINARY_DIR}/GenesisEngineState/Binaries/Win64/Editor\nDefault install: runtime only\nOptional editor install: GENESIS_INSTALL_EDITOR_APP=ON\nEditor-only UI/tools remain excluded from runtime packaging by target split and compile definitions.\n")

option(GENESIS_INSTALL_EDITOR_APP "Install the editor target alongside shipping runtime artifacts" OFF)
if (TARGET genesis_runtime)
    install(TARGETS genesis_runtime RUNTIME DESTINATION GenesisEngineState/Binaries/Win64/Runtime COMPONENT RuntimeApp)
    if (WIN32)
        install(FILES ${EW_SPV} DESTINATION GenesisEngineState/Binaries/Win64/Runtime/shaders COMPONENT RuntimeApp)
    endif()
endif()
if (WIN32)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/genesis.ico" DESTINATION GenesisEngineState/Binaries/Win64 COMPONENT RuntimeApp)
endif()
install(FILES "${GENESIS_RUNTIME_EDITOR_SPLIT_MANIFEST}" DESTINATION GenesisEngineState/Binaries/Win64 COMPONENT RuntimeApp)
if (GENESIS_INSTALL_EDITOR_APP)
    install(TARGETS genesis_app RUNTIME DESTINATION GenesisEngineState/Binaries/Win64/Editor COMPONENT EditorApp)
    if (WIN32)
        install(FILES ${EW_SPV} DESTINATION GenesisEngineState/Binaries/Win64/Editor/shaders COMPONENT EditorApp)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/genesis.ico" DESTINATION GenesisEngineState/Binaries/Win64 COMPONENT EditorApp)
    endif()
endif()

# Hard fail if someone configures a 32-bit Windows build.
if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "GenesisEngine Vulkan apps must be built as Win64 (x64). Configure with -A x64.")
    endif()
endif()
