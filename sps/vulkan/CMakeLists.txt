set(TARGET_NAME engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
  
find_package(spdlog CONFIG)


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
  ../tools/cla_parser.cpp
  )

target_include_directories(${TARGET_NAME}
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/../..")

target_link_libraries(${TARGET_NAME}
  spdlog::spdlog
  ${GLFW_STATIC_LIBRARIES}
  ${Vulkan_LIBRARY}
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}
)

target_compile_options(
  ${TARGET_NAME} PRIVATE
  "$<$<CONFIG:Debug>:-DSPS_DEBUG>"
  "$<$<CONFIG:Release>:>"
)

add_subdirectory(shaders)



if (0)
string(
  APPEND _opts
  "$<IF:$<CXX_COMPILER_ID:MSVC>,"
  "/W4;$<$<CONFIG:RELEASE>:/O2>,"
  "-Wall;-Wextra;-Werror;"
  "$<$<CONFIG:RELEASE>:-O3>"
  "$<$<CXX_COMPILER_ID:Clang>:-stdlib=libc++>"
  ">"
  )
add_compile_options("${_opts}")
endif()


