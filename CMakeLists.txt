cmake_minimum_required(VERSION 3.8...3.12 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Extra policies (not covering all introduced after 3.12)
foreach(policy
    CMP0083 # CMake 3.14
    CMP0127
    # Add more here
    )
  if(POLICY ${policy})
    cmake_policy(SET ${policy} NEW)
  endif()
endforeach()

include(CMakeDependentOption)

project(Vulkanstein3D
  VERSION 1.0
  DESCRIPTION "Vulkanstein3D is an open-source 2D rendering engine written in C/C++"
  LANGUAGES
  C
  CXX)


option(VULK3D_FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)

if (VULK3D_FORCE_COLORED_OUTPUT)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics)
  endif()
endif()

if (NOT CMAKE_VERSION VERSION_LESS 3.7.0)
  message(STATUS "Using module to find Vulkan")
  find_package(Vulkan)
else()
  message(FATAL_ERROR "Could not find Vulkan library!")  
endif()

option(VULK3D_DISPATCH_LOADER_DYNAMIC OFF)
if (VULK3D_DISPATCH_LOADER_DYNAMIC)
  set(SPS_VULKAN_DISPATCH_LOADER_DYNAMIC 1)
endif()

add_subdirectory(debugMessenger)

#add_subdirectory(engine)
add_subdirectory(sps)

add_subdirectory(vulk3D)
