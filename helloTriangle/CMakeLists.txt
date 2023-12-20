set(TARGET_NAME triangle)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
  
add_executable(${TARGET_NAME}
  main.cpp
  )

target_link_libraries(${TARGET_NAME}
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
