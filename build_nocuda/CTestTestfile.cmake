# CMake generated Testfile for 
# Source directory: /mnt/data/gen/Draft Container/GenesisEngine
# Build directory: /mnt/data/gen/Draft Container/GenesisEngine/build_nocuda
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(deterministic "/mnt/data/gen/Draft Container/GenesisEngine/build_nocuda/test_determinism")
set_tests_properties(deterministic PROPERTIES  _BACKTRACE_TRIPLES "/mnt/data/gen/Draft Container/GenesisEngine/CMakeLists.txt;215;add_test;/mnt/data/gen/Draft Container/GenesisEngine/CMakeLists.txt;0;")
add_test(ai_golden "/mnt/data/gen/Draft Container/GenesisEngine/build_nocuda/test_ai_golden")
set_tests_properties(ai_golden PROPERTIES  _BACKTRACE_TRIPLES "/mnt/data/gen/Draft Container/GenesisEngine/CMakeLists.txt;255;add_test;/mnt/data/gen/Draft Container/GenesisEngine/CMakeLists.txt;0;")
add_test(microprocessor "/mnt/data/gen/Draft Container/GenesisEngine/build_nocuda/test_microprocessor")
set_tests_properties(microprocessor PROPERTIES  _BACKTRACE_TRIPLES "/mnt/data/gen/Draft Container/GenesisEngine/CMakeLists.txt;261;add_test;/mnt/data/gen/Draft Container/GenesisEngine/CMakeLists.txt;0;")
subdirs("runtime_cli")
