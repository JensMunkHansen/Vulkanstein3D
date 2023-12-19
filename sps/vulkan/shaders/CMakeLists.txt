find_program(GLSL_VALIDATOR glslangValidator)
if(NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found!")
endif()

set(SHADERS
  fragment.frag
  vertex.vert
)

foreach(SHADER ${SHADERS})
  get_filename_component(FILE_NAME ${SHADER} NAME_WLE)
  set(SPIRV ${CMAKE_CURRENT_BINARY_DIR}/${FILE_NAME}.spv)
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPIRV}
    DEPENDS ${SHADER}
  )

  list(APPEND SPIRV_FILES ${SPIRV})
endforeach()

source_group("Shader Files" FILES ${SHADERS})

add_custom_target(vulkan-shaders DEPENDS ${SPIRV_FILES} SOURCES ${SHADERS})
