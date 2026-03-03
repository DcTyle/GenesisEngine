cmake_minimum_required(VERSION 3.18)
project(GenesisEngine)

# Disallow in-source builds to keep the repo clean (no build artifacts in tree).
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are disabled. Use: cmake -S . -B build")
endif()

# Optional asset import utilities (assimp). OFF by default to keep the core
# build minimal and deterministic.
option(EW_BUILD_ASSIMP_TOOLS "Build optional assimp-based import tools" OFF)
if (EW_BUILD_ASSIMP_TOOLS)
    find_package(assimp CONFIG REQUIRED)
endif()

set(CMAKE_CXX_STANDARD 17)

# CUDA build policy: fixed toolchain behavior for deterministic kernels.
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Default CUDA architectures: RTX 2060 (Turing) = 75. Override via -DCMAKE_CUDA_ARCHITECTURES=...
if (EW_ENABLE_CUDA AND NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

# Carrier-trace (substrate microprocessor inspection) can be compiled out for FPS.
# Semantics of arithmetic remain identical either way.
option(EW_ENABLE_CARRIER_TRACE "Enable carrier-traced micro-op recording" OFF)
if (EW_ENABLE_CARRIER_TRACE)
    add_compile_definitions(EW_ALU_TRACE_ENABLE=1)
else()
    add_compile_definitions(EW_ALU_TRACE_ENABLE=0)
endif()

# Optional verification harness (Blueprint v7 V31-A).
option(EW_BUILD_RUNTIME_CLI "Build deterministic runtime_cli harness" ON)

# Windows build policy:
# EigenWareCore relies on true 128-bit integer arithmetic (e.g., __int128 / __uint128_t)
# to preserve exact fixed-point behavior as specified. MSVC does not support __int128.
# Therefore, Windows builds MUST use clang-cl (Visual Studio toolset: ClangCL).
if (WIN32)
    # Hard requirement: Win64 only.
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "Genesis Engine Vulkan app requires Win64/x64. Re-configure with -A x64.")
    endif()
    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "EigenWareCore requires clang-cl on Windows (MSVC lacks __int128 support).\n"
            "Re-configure with the ClangCL toolset, e.g.:\n"
            "  cmake -S . -B build_core -G \"Visual Studio 17 2022\" -A x64 -T ClangCL\n")
    endif()
endif()

add_library(EigenWareCore
    src/GE_runtime.cpp
    src/GE_camera_anchor.cpp
    src/GE_project_settings.cpp
    src/GE_neural_phase_ai.cpp
    src/GE_ai_policy.cpp
    src/GE_ai_anticipation.cpp
    src/GE_metric_registry.cpp
    src/GE_language_foundation.cpp
    src/GE_audio_wav.cpp
    src/GE_audio_features.cpp
    src/GE_speech_alignment.cpp
    src/GE_voice_synth.cpp
    src/GE_g2p_lexicon.cpp
    src/GE_voice_predictive_model.cpp
    src/GE_math_foundation.cpp
    src/GE_experiment_templates.cpp
    src/GE_domain_policy.cpp
    src/GE_rate_limiter.cpp
    src/GE_live_crawler.cpp
    src/GE_curriculum_manager.cpp
    src/GE_learning_checkpoint_gate.cpp
    src/GE_learning_automation.cpp
    src/code_artifact_ops.cpp
    src/inspector_fields.cpp
    src/anchor_pack.cpp
    src/ew_id9.cpp
    src/coherence_gate.cpp
    src/hydrator.cpp
    src/GE_operator_registry.cpp
    src/GE_corpus_allowlist.cpp
    src/GE_corpus_canonicalizer.cpp
    src/GE_corpus_anchor_store.cpp
    src/GE_corpus_pulse_log.cpp
    src/GE_lane_threshold_schedule.cpp
    src/GE_overlap_router.cpp
    src/GE_quantity_peeling_trainer.cpp
    src/GE_coherence_graph_store.cpp
    src/GE_safety_governor.cpp
    src/GE_phase_current.cpp
    src/GE_global_coherence.cpp
    src/ancilla_ops.cpp
    src/substrate_alu.cpp
    src/substrate_harmonics.cpp
    src/harmonic_fingerprint.cpp
    src/crawler_subsystem.cpp
    src/delta_profiles.cpp
    src/qubit_lanes.cpp
    src/text_encoder.cpp
    src/bytes_encoder.cpp
    # CUDA-only glue (GPU-resident crawler/learning kernels). Added below when EW_ENABLE_CUDA is ON.
    src/GE_object_memory.cpp
    src/GE_uv_unwrap.cpp
    src/GE_uv_atlas_baker.cpp
    src/GE_shell_mesh.cpp
    src/GE_object_ancilla.cpp
    src/GE_object_local_cuda.cpp
    src/ew_cordic.cpp
    src/spec_aux_ops.cpp
    src/canonical_ops.cpp
    src/frequency_collapse.cpp
    src/cmb_bath.cpp
    src/operator_validator.cpp
    src/ew_phase_transport.cpp
    src/ew_substrate_manager.cpp
    src/ew_runtime.cpp
    src/ew_eq_exec.cpp
    src/ew_flow_ir.cpp
    src/ew_coherence_analyzer.cpp

    src/ewmesh_voxelizer.cpp

    # Genesis lattice (CPU reference implementation)
    src/field_lattice_cpu.cpp

)
target_include_directories(EigenWareCore PUBLIC include)

# -----------------------------------------------------------------------------
# AnchorPack: embed selected repo process artifacts as carrier-encoded anchors
# -----------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(EW_ANCHOR_PACK_LIST "${CMAKE_CURRENT_SOURCE_DIR}/scripts/anchor_pack_list.txt")
set(EW_ANCHOR_PACK_GEN_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/anchor_pack_blobs.cpp")
add_custom_command(
    OUTPUT "${EW_ANCHOR_PACK_GEN_CPP}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/anchorize_files.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --list "${EW_ANCHOR_PACK_LIST}"
            --out "${EW_ANCHOR_PACK_GEN_CPP}"
    DEPENDS "${EW_ANCHOR_PACK_LIST}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/anchorize_files.py"
    VERBATIM
)
target_sources(EigenWareCore PRIVATE "${EW_ANCHOR_PACK_GEN_CPP}")
target_include_directories(EigenWareCore PRIVATE "/generated")

# ----------------------------------------------------------------------------

option(EW_BUILD_VULKAN_APP "Build Vulkan native viewport app" ON)
set_target_properties(EigenWareCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (UNIX)
    target_link_libraries(EigenWareCore PUBLIC m)
endif()


if (EW_BUILD_ASSIMP_TOOLS)
    target_sources(EigenWareCore PRIVATE src/assimp_fbx_io.cpp)
    target_link_libraries(EigenWareCore PUBLIC assimp::assimp)
endif()

# Main executables are provided by vulkan_app as genesis_runtime and genesis_editor.

option(EW_ENABLE_UI_PARTITION "Enable UI anchor partition + chat stream" ON)
option(EW_ENABLE_CUDA "Enable CUDA evolution kernel (required for GPU-only pipeline)" ON)
if (EW_ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    add_library(eigen_cuda
        cuda/evolve_kernel.cu
        cuda/field_lattice_kernels.cu
        cuda/emergent_post_kernel.cu
        cuda/carrier_bundle_kernels.cu
        cuda/learning_gate_kernels.cu
        cuda/crawler_encode_kernels.cu
        cuda/GE_overlap_kernels.cu
        cuda/GE_canonicalize_kernels.cu
        cuda/GE_topk_select_kernels.cu
        cuda/symbol_tokenize_kernels.cu
        cuda/GE_phase_current_kernels.cu
        cuda/GE_object_local_kernels.cu
)
    target_link_libraries(eigen_cuda PRIVATE CUDA::cudart)
    target_include_directories(eigen_cuda PRIVATE ${CUDAToolkit_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Deterministic CUDA numerics: disable FMA reassociation and enforce precise div/sqrt.
target_compile_options(eigen_cuda PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--fmad=false --prec-div=true --prec-sqrt=true>
)
    target_compile_definitions(EigenWareCore PRIVATE EW_ENABLE_CUDA=1)
    target_link_libraries(EigenWareCore PRIVATE eigen_cuda)

    # CUDA C++ wrappers (thin orchestration only; compute stays in kernels)
    target_sources(EigenWareCore PRIVATE
        src/crawler_encode_cuda.cpp
    src/symbol_tokenize_cuda.cpp
        src/carrier_bundle_cuda.cpp
        src/GE_canonicalize_cuda.cpp
        src/GE_overlap_cuda.cpp
        src/GE_topk_cuda.cpp
    )
else()
    # CPU-only build: compiles and links without CUDA. GPU-resident corpus pipeline will return
    # "not available" deterministically via existing EW_ENABLE_CUDA guards.
    target_compile_definitions(EigenWareCore PRIVATE EW_ENABLE_CUDA=0)
    target_sources(EigenWareCore PRIVATE
        src/carrier_bundle_cuda.cpp
    )
endif()

# Vulkan app lives in its own subdirectory.
if (EW_BUILD_VULKAN_APP)
    add_subdirectory(vulkan_app)
endif()

enable_testing()
add_executable(test_determinism
    tests/test_determinism.cpp
)
target_link_libraries(test_determinism PRIVATE EigenWareCore)
add_test(NAME deterministic COMMAND test_determinism)

# Corpus pipeline regression tests (CUDA required).
if (EW_ENABLE_CUDA)
add_test(NAME ge_corpus_mvp
    COMMAND ge_regression_suite
        corpus_dir=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/Corpus
        out_dir=${CMAKE_BINARY_DIR}/test_out/Corpus
        lane=0
        epochs=2
        tokens=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tokens_vocab.txt
)
set_tests_properties(ge_corpus_mvp PROPERTIES LABELS "CUDA;CORPUS")

add_test(NAME ge_corpus_replay_identity
    COMMAND ge_corpus_replay
        state_dir=${CMAKE_BINARY_DIR}/test_out/Corpus
        corpus_root=${CMAKE_BINARY_DIR}/test_out/Corpus
        epochs=2
        cuda=true
)
set_tests_properties(ge_corpus_replay_identity PROPERTIES LABELS "CUDA;CORPUS")


endif()

add_executable(ew_contract_harness
    tests/contract_harness.cpp
)
target_link_libraries(ew_contract_harness PRIVATE EigenWareCore)

add_executable(ew_verify_proofs
    tests/proofs_verify.cpp
)
target_link_libraries(ew_verify_proofs PRIVATE EigenWareCore)

add_executable(test_ai_golden
    tests/test_ai_golden.cpp
)
target_link_libraries(test_ai_golden PRIVATE EigenWareCore)
add_test(NAME ai_golden COMMAND test_ai_golden)

add_executable(test_microprocessor
    tests/test_microprocessor.cpp
)
target_link_libraries(test_microprocessor PRIVATE EigenWareCore)
add_test(NAME microprocessor COMMAND test_microprocessor)

add_executable(test_contact_constraint
    tests/test_contact_constraint.cpp
)
target_link_libraries(test_contact_constraint PRIVATE EigenWareCore)
add_test(NAME contact_constraint COMMAND test_contact_constraint)

add_executable(ew_crawler_loop
    src/tools/crawler_loop_main.cpp
)
target_link_libraries(ew_crawler_loop PRIVATE EigenWareCore)

add_executable(ew_encode_state
    src/tools/encode_state_main.cpp
)
target_link_libraries(ew_encode_state PRIVATE EigenWareCore)

add_executable(ew_rehydrate_state EXCLUDE_FROM_ALL
    src/tools/rehydrate_state_main.cpp
)
target_link_libraries(ew_rehydrate_state PRIVATE EigenWareCore)

add_executable(ew_ai_headless_run EXCLUDE_FROM_ALL
    src/tools/ai_headless_run_main.cpp
)
target_link_libraries(ew_ai_headless_run PRIVATE EigenWareCore)

add_executable(ew_hydrate_workspace EXCLUDE_FROM_ALL
    src/tools/hydrate_workspace_main.cpp
)
target_link_libraries(ew_hydrate_workspace PRIVATE EigenWareCore)

add_executable(ew_synthesize_code EXCLUDE_FROM_ALL
    src/tools/synthesize_code_main.cpp
)
target_link_libraries(ew_synthesize_code PRIVATE EigenWareCore)


add_executable(ew_toolchain_loop
    src/tools/toolchain_loop_main.cpp
)
target_link_libraries(ew_toolchain_loop PRIVATE EigenWareCore)

add_executable(ew_flow_analyze
    src/tools/flow_analyze_main.cpp
)
target_link_libraries(ew_flow_analyze PRIVATE EigenWareCore)

add_executable(ew_self_patch_loop
    src/tools/self_patch_loop_main.cpp
)
target_link_libraries(ew_self_patch_loop PRIVATE EigenWareCore)

add_executable(ge_corpus_ingest
    src/tools/ge_corpus_ingest_main.cpp
)
target_link_libraries(ge_corpus_ingest PRIVATE EigenWareCore)

add_executable(ge_corpus_replay
    src/tools/ge_corpus_replay_main.cpp
)
target_link_libraries(ge_corpus_replay PRIVATE EigenWareCore)

add_executable(ge_wikimedia_dump_extract
    src/tools/ge_wikimedia_dump_extract_main.cpp
)
target_link_libraries(ge_wikimedia_dump_extract PRIVATE EigenWareCore)


add_executable(ge_speech_ingest
    src/tools/ge_speech_ingest_main.cpp
)
target_link_libraries(ge_speech_ingest PRIVATE EigenWareCore)

add_executable(ge_tts_speak
    src/tools/ge_tts_speak_main.cpp
)
target_link_libraries(ge_tts_speak PRIVATE EigenWareCore)

add_executable(ge_voice_train
    src/tools/ge_voice_train_main.cpp
)
target_link_libraries(ge_voice_train PRIVATE EigenWareCore)


option(EW_ENFORCE_COHERENCE_GATE "Fail builds unless integration proofs pass" ON)
if (EW_ENFORCE_COHERENCE_GATE)
    add_custom_target(coherence_gate ALL
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND ew_contract_harness
        COMMAND ew_verify_proofs
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Coherence gate: running integration proofs"
        VERBATIM
    )
    add_dependencies(coherence_gate test_determinism test_ai_golden test_microprocessor ew_contract_harness ew_verify_proofs)
    # Executables are provided by vulkan_app; enforce coherence gate at build level.
endif()

add_custom_target(ew_package_artifacts
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/artifacts
    COMMAND ew_encode_state ${CMAKE_BINARY_DIR}/artifacts/ew_state.bin ${CMAKE_BINARY_DIR}/artifacts/ew_state_meta.txt
    COMMENT "Packaging encoder + rehydrater artifacts"
    VERBATIM
)

if (EW_BUILD_RUNTIME_CLI)
    add_subdirectory(runtime_cli)
endif()

if (EW_BUILD_ASSIMP_TOOLS)
    add_executable(ew_assimp_fbx_test src/tools/assimp_fbx_test_main.cpp)
    target_link_libraries(ew_assimp_fbx_test PRIVATE EigenWareCore)
    target_include_directories(ew_assimp_fbx_test PRIVATE include)
endif()
add_executable(ge_vocab_recall_test
    src/tools/ge_vocab_recall_test_main.cpp
)
target_link_libraries(ge_vocab_recall_test PRIVATE EigenWareCore)

add_executable(ge_regression_suite
    src/tools/ge_regression_suite_main.cpp
)
target_link_libraries(ge_regression_suite PRIVATE EigenWareCore)