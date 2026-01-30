# Download dependencies by using FetchContent_Declare
# Use FetchContent_MakeAvailable only in those code parts where the dependency is actually needed

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.0.1
    GIT_PROGRESS ON
    FIND_PACKAGE_ARGS 3.0.1)

