function(ew_find_spirv_compiler out_compiler out_mode)
    if (WIN32)
        find_program(_ew_glslc glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)
        find_program(_ew_glslang glslangValidator HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)
    else()
        find_program(_ew_glslc glslc)
        find_program(_ew_glslang glslangValidator)
    endif()

    if (_ew_glslc)
        set(${out_compiler} "${_ew_glslc}" PARENT_SCOPE)
        set(${out_mode} "glslc" PARENT_SCOPE)
    elseif (_ew_glslang)
        set(${out_compiler} "${_ew_glslang}" PARENT_SCOPE)
        set(${out_mode} "glslang" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Shader compiler not found. Install Vulkan SDK (glslc or glslangValidator).")
    endif()
endfunction()

function(ew_add_spirv_shader_command compiler mode source_file output_file)
    if ("${mode}" STREQUAL "glslc")
        add_custom_command(
            OUTPUT "${output_file}"
            COMMAND "${compiler}" -o "${output_file}" "${source_file}"
            DEPENDS "${source_file}"
            VERBATIM)
    elseif ("${mode}" STREQUAL "glslang")
        add_custom_command(
            OUTPUT "${output_file}"
            COMMAND "${compiler}" -V "${source_file}" -o "${output_file}"
            DEPENDS "${source_file}"
            VERBATIM)
    else()
        message(FATAL_ERROR "Unsupported SPIR-V shader compiler mode: ${mode}")
    endif()
endfunction()
