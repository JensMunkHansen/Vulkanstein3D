# Download dependencies by using FetchContent_Declare
# Use FetchContent_MakeAvailable only in those code parts where the dependency is actually needed
#
# System-installed packages are preferred by default. Set VULK3D_USE_SYSTEM_<NAME>=OFF
# to force fetching a specific dependency via FetchContent.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Ensure Git LFS files are pulled for this repository
find_program(GIT_LFS git-lfs)
if(GIT_LFS)
  execute_process(
    COMMAND ${GIT_LFS} pull
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()

# --- Options for using system-installed packages ---
option(VULK3D_USE_SYSTEM_VMA    "Use system-installed VulkanMemoryAllocator" ON)
option(VULK3D_USE_SYSTEM_SPDLOG "Use system-installed spdlog"               ON)
option(VULK3D_USE_SYSTEM_TOML11 "Use system-installed toml11"               ON)
option(VULK3D_USE_SYSTEM_GLFW   "Use system-installed GLFW"                 ON)
option(VULK3D_USE_SYSTEM_IMGUI  "Use system-installed imgui"                ON)
option(VULK3D_USE_SYSTEM_CGLTF  "Use system-installed cgltf"                ON)
option(VULK3D_USE_SYSTEM_STB    "Use system-installed stb"                  ON)

# --- Try to find system packages ---
if(VULK3D_USE_SYSTEM_VMA)
  find_package(VulkanMemoryAllocator 3.0.1 QUIET)
endif()

if(VULK3D_USE_SYSTEM_SPDLOG)
  find_package(spdlog QUIET)
endif()

if(VULK3D_USE_SYSTEM_TOML11)
  find_package(toml11 QUIET)
endif()

if(VULK3D_USE_SYSTEM_GLFW)
  find_package(glfw3 QUIET)
endif()

if(VULK3D_USE_SYSTEM_IMGUI)
  find_package(imgui CONFIG QUIET)
endif()

if(VULK3D_USE_SYSTEM_CGLTF)
  find_path(CGLTF_INCLUDE_DIR cgltf.h)
  if(CGLTF_INCLUDE_DIR)
    set(cgltf_FOUND TRUE)
    message(STATUS "Found system cgltf: ${CGLTF_INCLUDE_DIR}")
  endif()
endif()

if(VULK3D_USE_SYSTEM_STB)
  find_path(STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb)
  if(STB_INCLUDE_DIR)
    set(stb_FOUND TRUE)
    message(STATUS "Found system stb: ${STB_INCLUDE_DIR}")
  endif()
endif()

# --- FetchContent declarations (used as fallback when system packages are not found) ---
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.0.1
    GIT_PROGRESS ON)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.6
    GIT_PROGRESS ON)

FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14
    GIT_PROGRESS ON)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_PROGRESS ON)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.1
    GIT_PROGRESS ON)

FetchContent_Declare(toml11
    GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
    GIT_TAG v4.2.0
    GIT_PROGRESS ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_PROGRESS ON)

