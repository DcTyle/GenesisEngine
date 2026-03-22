# Install script for directory: D:/GameCreation/GenesisEngine/vulkan_app

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "D:/GameCreation/GenesisEngine/out/install/win64-vs2022-clang-vulkan")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "RuntimeApp" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Runtime" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Runtime/Debug/GenesisRuntime.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Runtime" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Runtime/Release/GenesisRuntime.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Runtime" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Runtime/MinSizeRel/GenesisRuntime.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Runtime" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Runtime/RelWithDebInfo/GenesisRuntime.exe")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "RuntimeApp" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Runtime/shaders" TYPE FILE FILES
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/instanced.vert.spv"
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/instanced.frag.spv"
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/cam_hist.comp.spv"
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/cam_median.comp.spv"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "RuntimeApp" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Branding" TYPE FILE FILES
    "D:/GameCreation/GenesisEngine/vulkan_app/resources/genesis.ico"
    "D:/GameCreation/GenesisEngine/vulkan_app/resources/genesis_splash.bmp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "RuntimeApp" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64" TYPE FILE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/GENESIS_RUNTIME_EDITOR_SPLIT_MANIFEST.txt")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "EditorApp" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Editor" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Editor/Debug/GenesisEngine.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Editor" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Editor/Release/GenesisEngine.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Editor" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Editor/MinSizeRel/GenesisEngine.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Editor" TYPE EXECUTABLE FILES "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/GenesisEngineState/Binaries/Win64/Editor/RelWithDebInfo/GenesisEngine.exe")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "EditorApp" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/GenesisEngineState/Binaries/Win64/Editor/shaders" TYPE FILE FILES
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/instanced.vert.spv"
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/instanced.frag.spv"
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/cam_hist.comp.spv"
    "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/shaders/cam_median.comp.spv"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/GameCreation/GenesisEngine/out/build/win64-vs2022-clang-vulkan/vulkan_app/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
