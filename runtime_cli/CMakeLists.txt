add_executable(ew_runtime_cli EXCLUDE_FROM_ALL
    main.cpp
)

target_include_directories(ew_runtime_cli PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
)
