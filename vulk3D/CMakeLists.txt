if (UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_search_module(GLFW REQUIRED glfw3)
endif()

if (UNIX)
  add_subdirectory(shaders)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/launch.sh.in" "${CMAKE_CURRENT_BINARY_DIR}/launch.sh" @ONLY)

if (0)
set(TARGET_NAME vulk3D)
add_executable(${TARGET_NAME} main.cpp)

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET_NAME}
  PUBLIC
  engine
  PRIVATE
  ${GLFW_STATIC_LIBRARIES}
  ${Vulkan_LIBRARY})


endif()
add_executable(app main2.cpp)

# All dynamic loading
target_link_libraries(app
  PUBLIC
  engine
  Vulkan::Headers
  PRIVATE
  ${GLFW_STATIC_LIBRARIES}
)
if (NOT SPS_VULKAN_DISPATCH_LOADER_DYNAMIC)
  target_link_libraries(app
    PRIVATE
    ${Vulkan_LIBRARY}  
  )
endif()

add_custom_command(TARGET app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/vulk3D.toml
  ${CMAKE_CURRENT_BINARY_DIR}/vulk3D.toml)

# ImGui-enabled version
add_executable(app_imgui main_imgui.cpp)

target_link_libraries(app_imgui
  PUBLIC
  engine
  imgui_impl
  Vulkan::Headers
  PRIVATE
  ${GLFW_STATIC_LIBRARIES}
)
if (NOT SPS_VULKAN_DISPATCH_LOADER_DYNAMIC)
  target_link_libraries(app_imgui
    PRIVATE
    ${Vulkan_LIBRARY}
  )
endif()

add_custom_command(TARGET app_imgui POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/vulk3D.toml
  ${CMAKE_CURRENT_BINARY_DIR}/vulk3D.toml)
