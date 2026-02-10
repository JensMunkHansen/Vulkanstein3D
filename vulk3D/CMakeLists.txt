
if (UNIX)
  add_subdirectory(shaders)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/launch.sh.in" "${CMAKE_CURRENT_BINARY_DIR}/launch.sh" @ONLY)

# Compute relative path from binary output directory to source data directory.
# This is substituted into vulk3D.toml.in so asset paths work regardless of
# build directory layout (single-config vs multi-config).
get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_isMultiConfig)
  # Multi-config: binaries go to ${CMAKE_BINARY_DIR}/<Config>/${CMAKE_INSTALL_BINDIR}
  # All configs have the same depth, so use "Release" as representative.
  file(RELATIVE_PATH DATA_DIR
    "${CMAKE_BINARY_DIR}/Release/${CMAKE_INSTALL_BINDIR}"
    "${CMAKE_SOURCE_DIR}/data")
  file(RELATIVE_PATH LOCAL_DATA_DIR
    "${CMAKE_BINARY_DIR}/Release/${CMAKE_INSTALL_BINDIR}"
    "${CMAKE_SOURCE_DIR}/localdata")
else()
  file(RELATIVE_PATH DATA_DIR
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
    "${CMAKE_SOURCE_DIR}/data")
  file(RELATIVE_PATH LOCAL_DATA_DIR
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
    "${CMAKE_SOURCE_DIR}/localdata")
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/vulk3D.toml.in"
  "${CMAKE_CURRENT_BINARY_DIR}/vulk3D.toml" @ONLY)

if (0)
set(TARGET_NAME vulk3D)
add_executable(${TARGET_NAME} main.cpp)

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET_NAME}
  PUBLIC
  engine
  PRIVATE
  ${GLFW_TARGET}
  ${Vulkan_LIBRARY})


endif()
add_executable(app main2.cpp)

# All dynamic loading
target_link_libraries(app
  PUBLIC
  engine
  Vulkan::Headers
  PRIVATE
  ${GLFW_TARGET}
)
if (NOT SPS_VULKAN_DISPATCH_LOADER_DYNAMIC)
  target_link_libraries(app
    PRIVATE
    ${Vulkan_LIBRARY}
  )
endif()

add_custom_command(TARGET app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/vulk3D.toml
  $<TARGET_FILE_DIR:app>/vulk3D.toml)

# ImGui-enabled version
add_executable(app_imgui main_imgui.cpp)

target_link_libraries(app_imgui
  PUBLIC
  engine
  imgui_impl
  Vulkan::Headers
  PRIVATE
  ${GLFW_TARGET}
)
if (NOT SPS_VULKAN_DISPATCH_LOADER_DYNAMIC)
  target_link_libraries(app_imgui
    PRIVATE
    ${Vulkan_LIBRARY}
  )
endif()

add_custom_command(TARGET app_imgui POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/vulk3D.toml
  $<TARGET_FILE_DIR:app_imgui>/vulk3D.toml)
