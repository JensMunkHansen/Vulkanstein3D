# Download dependencies by using FetchContent_Declare
# Use FetchContent_MakeAvailable only in those code parts where the dependency is actually needed

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

if (0)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
    GIT_SHALLOW ON
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 3.3.8)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
    GIT_SHALLOW ON
    GIT_PROGRESS ON)

FetchContent_Declare(gtest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.13.0
    GIT_SHALLOW ON
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 1.13.0)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.89.4
    GIT_SHALLOW ON
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 1.89.4)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.11.0
    GIT_SHALLOW ON
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 1.11.0)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 5736b15f7ea0ffb08dd38af21067c314d6a3aae9
    GIT_PROGRESS ON)

FetchContent_Declare(toml
    GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
    GIT_TAG v3.7.1
    GIT_SHALLOW ON
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 3.7.1)

FetchContent_Declare(Vulkan
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers
    GIT_TAG v1.3.215
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 1.3.215)
  
endif()



FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.0.1
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 3.0.1)

