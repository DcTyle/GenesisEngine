cmake_minimum_required(VERSION 3.20)
project(VulkanGpuCalibrationKernels LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_executable(vulkan_gpu_calibration
    src/main.cpp
)

target_include_directories(vulkan_gpu_calibration PRIVATE include)
target_link_libraries(vulkan_gpu_calibration PRIVATE Vulkan::Vulkan)
