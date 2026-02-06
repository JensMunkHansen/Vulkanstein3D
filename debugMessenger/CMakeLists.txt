
add_executable(debugMessenger
  instance.cpp
  main.cpp
)
target_include_directories(debugMessenger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(debugMessenger PRIVATE
#  ${CMAKE_THREAD_LIBS_INIT}  
  glfw
#  ${CMAKE_DL_LIBS}
${Vulkan_LIBRARY}
pthread
)

add_executable(main0 main0.cpp)
target_link_libraries(main0 PRIVATE
  ${Vulkan_LIBRARY}
)

add_executable(main1 main1.cpp)
target_link_libraries(main1 PRIVATE
  glfw
  ${Vulkan_LIBRARY}
)

add_executable(main2 main2.cpp instance.cpp)
target_link_libraries(main2 PUBLIC
  glfw
  ${Vulkan_LIBRARY}
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}
)


add_executable(main3 main2.cpp instance2.cpp)
target_link_libraries(main3 PUBLIC
  glfw
  ${Vulkan_LIBRARY}
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}
)

