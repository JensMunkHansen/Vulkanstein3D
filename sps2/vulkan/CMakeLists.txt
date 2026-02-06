set(TARGET_NAME engine)

find_package(spdlog CONFIG REQUIRED)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

add_library(${TARGET_NAME}
  make_info.cpp
  engine.cpp
  window.cpp
  renderer.cpp
  exception.cpp
  representation.cpp
  instance.cpp
  logging.cpp
  ../tools/cla_parser.cpp
app.cpp)

target_include_directories(${TARGET_NAME}
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/../..")

target_link_libraries(${TARGET_NAME}
  spdlog::spdlog
  ${CMAKE_THREAD_LIBS_INIT}  
  glfw
  ${CMAKE_DL_LIBS}
)

# TODO: If loading dynamically, add this to any compilation unit
# VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

if (NOT SPS_VULKAN_DISPATCH_LOADER_DYNAMIC)
  target_link_libraries(${TARGET_NAME}
    ${Vulkan_LIBRARY}  
  ) 
endif()


target_compile_options(
  ${TARGET_NAME} PRIVATE
  "$<$<CONFIG:Debug>:-DSPS_DEBUG>"
  "$<$<CONFIG:Release>:>"
)

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


