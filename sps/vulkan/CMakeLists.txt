set(TARGET_NAME engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch spdlog
FetchContent_MakeAvailable(spdlog)

# Fetch toml11
FetchContent_MakeAvailable(toml11)

# Fetch GLFW
FetchContent_MakeAvailable(glfw)

# Fetch and build ImGui
FetchContent_MakeAvailable(imgui)

# Fetch cgltf (header-only glTF parser)
FetchContent_MakeAvailable(cgltf)

# Fetch stb (stb_image for texture loading)
FetchContent_MakeAvailable(stb)

# Create ImGui library with Vulkan and GLFW backends
add_library(imgui_impl STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui_impl PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_impl PUBLIC
  glfw
  Vulkan::Vulkan
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

add_library(${TARGET_NAME}
  window.cpp
  instance.cpp
  representation.cpp
  device.cpp
  device_simple.cpp
  renderer.cpp
  fence.cpp
  frame.cpp
  app.cpp
  shaders.cpp
  semaphore.cpp
  exception.cpp
  swapchain.cpp
  swapchain_simple.cpp
  pipeline.cpp
  windowsurface.cpp
  commands.cpp
  framebuffer.cpp
  camera.cpp
  buffer.cpp
  descriptor_builder.cpp
  mesh.cpp
  ply_loader.cpp
  miniply.cpp
  gltf_loader.cpp
  texture.cpp
  ibl.cpp
  screenshot.cpp
  command_file.cpp
  acceleration_structure.cpp
  raytracing_pipeline.cpp
  ../tools/cla_parser.cpp
  )

target_include_directories(${TARGET_NAME}
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/../.."
  PRIVATE "${cgltf_SOURCE_DIR}"
  PRIVATE "${stb_SOURCE_DIR}")

target_link_libraries(${TARGET_NAME}
  spdlog::spdlog
  toml11::toml11
  glfw
  ${Vulkan_LIBRARY}
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}
  imgui_impl
)

# Enable Vulkan dynamic dispatch for extension functions (ray tracing, etc.)
# This MUST be defined before any vulkan.hpp includes
target_compile_definitions(${TARGET_NAME}
  PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

target_compile_options(
  ${TARGET_NAME} PRIVATE
  "$<$<CONFIG:Debug>:-DSPS_DEBUG>"
  "$<$<CONFIG:Release>:>"
)

add_subdirectory(shaders)
add_dependencies(engine vulkan-shaders)

