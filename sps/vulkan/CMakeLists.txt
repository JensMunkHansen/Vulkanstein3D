set(TARGET_NAME engine)

# --- Resolve dependencies (prefer system, fall back to FetchContent) ---

# spdlog
if(TARGET spdlog::spdlog)
  set(SPDLOG_TARGET spdlog::spdlog)
  message(STATUS "spdlog: using system package")
elseif(TARGET spdlog)
  set(SPDLOG_TARGET spdlog)
  message(STATUS "spdlog: using system package (spdlog)")
else()
  message(STATUS "spdlog: using FetchContent")
  FetchContent_MakeAvailable(spdlog)
  set(SPDLOG_TARGET spdlog::spdlog)
endif()

# toml11
if(TARGET toml11::toml11)
  set(TOML11_TARGET toml11::toml11)
  message(STATUS "toml11: using system package")
elseif(TARGET toml11)
  set(TOML11_TARGET toml11)
  message(STATUS "toml11: using system package (toml11)")
else()
  message(STATUS "toml11: using FetchContent")
  FetchContent_MakeAvailable(toml11)
  set(TOML11_TARGET toml11::toml11)
endif()

# glfw — system find_package(glfw3) may create 'glfw' or 'glfw3::glfw3'
if(TARGET glfw)
  set(GLFW_TARGET glfw)
  message(STATUS "glfw: using system package")
elseif(TARGET glfw3::glfw3)
  set(GLFW_TARGET glfw3::glfw3)
  message(STATUS "glfw: using system package (glfw3::glfw3)")
else()
  message(STATUS "glfw: using FetchContent")
  FetchContent_MakeAvailable(glfw)
  set(GLFW_TARGET glfw)
endif()

# imgui
if(NOT imgui_FOUND)
  message(STATUS "imgui: using FetchContent")
  FetchContent_MakeAvailable(imgui)
  set(_IMGUI_FROM_FETCH ON)
else()
  message(STATUS "imgui: using system package")
  set(_IMGUI_FROM_FETCH OFF)
endif()

# cgltf (header-only glTF parser)
if(NOT cgltf_FOUND)
  message(STATUS "cgltf: using FetchContent")
  FetchContent_MakeAvailable(cgltf)
  set(CGLTF_INCLUDE_DIR "${cgltf_SOURCE_DIR}")
else()
  message(STATUS "cgltf: using system package")
endif()

# stb (stb_image for texture loading)
if(NOT stb_FOUND)
  message(STATUS "stb: using FetchContent")
  FetchContent_MakeAvailable(stb)
  set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
else()
  message(STATUS "stb: using system package")
endif()

# --- Build ImGui backend library ---
if(_IMGUI_FROM_FETCH)
  # Build from fetched sources with Vulkan and GLFW backends
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
    ${GLFW_TARGET}
    Vulkan::Vulkan
  )
else()
  # System imgui (e.g. vcpkg) — expose as imgui_impl interface target
  add_library(imgui_impl INTERFACE)
  target_link_libraries(imgui_impl INTERFACE imgui::imgui)
endif()

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
  app_config.cpp
  scene_manager.cpp
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
  render_graph.cpp
  stages/ui_stage.cpp
  stages/debug_2d_stage.cpp
  stages/raster_opaque_stage.cpp
  stages/raster_blend_stage.cpp
  stages/ray_tracing_stage.cpp
  stages/composite_stage.cpp
  stages/sss_blur_stage.cpp
  ../tools/cla_parser.cpp
  )

target_include_directories(${TARGET_NAME}
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/../.."
  PRIVATE "${CGLTF_INCLUDE_DIR}"
  PRIVATE "${STB_INCLUDE_DIR}")

target_link_libraries(${TARGET_NAME}
  PUBLIC build
  ${SPDLOG_TARGET}
  ${TOML11_TARGET}
  ${GLFW_TARGET}
  ${Vulkan_LIBRARY}
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}
  imgui_impl
)

# Enable Vulkan dynamic dispatch for extension functions (ray tracing, etc.)
# This MUST be defined before any vulkan.hpp includes
target_compile_definitions(${TARGET_NAME}
  PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
  PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_compile_options(
  ${TARGET_NAME} PRIVATE
  "$<$<CONFIG:Debug>:-DSPS_DEBUG>"
  "$<$<CONFIG:Release>:>"
)

add_subdirectory(shaders)
add_dependencies(engine vulkan-shaders)

