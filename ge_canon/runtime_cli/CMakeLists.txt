add_executable(ew_runtime_cli
    main.cpp
)

target_include_directories(ew_runtime_cli PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(ew_runtime_cli PRIVATE EigenWareCore)
