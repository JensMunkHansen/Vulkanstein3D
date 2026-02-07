find_program(GLSL_VALIDATOR glslangValidator)
if(NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found!")
endif()

# Shared include files (used via #include "tonemap.glsl" etc.)
set(SHADER_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/tonemap.glsl
)

# Shaders that use #include and need the include dir + dependency tracking
set(SHADERS_WITH_INCLUDES
  fragment.frag
  blinn_phong.frag
)

set(SHADERS
  vertex.vert
  fragment_normals.frag
  vertex_normals.vert
  debug_uv.frag
  debug_normals.frag
  debug_basecolor.frag
  debug_metallic_roughness.frag
  debug_ao.frag
  debug_emissive.frag
  fullscreen_quad.vert
  debug_texture2d.frag
)

set(RT_SHADERS
  raygen.rgen
  miss.rmiss
  closesthit.rchit
)

# Compile shaders that use #include (need --include-dir)
foreach(SHADER ${SHADERS_WITH_INCLUDES})
  get_filename_component(FILE_NAME ${SHADER} NAME_WLE)
  set(SPIRV ${CMAKE_CURRENT_BINARY_DIR}/${FILE_NAME}.spv)
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V -I${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPIRV}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} ${SHADER_INCLUDES}
  )

  list(APPEND SPIRV_FILES ${SPIRV})
endforeach()

# Compile plain shaders (no includes)
foreach(SHADER ${SHADERS})
  get_filename_component(FILE_NAME ${SHADER} NAME_WLE)
  set(SPIRV ${CMAKE_CURRENT_BINARY_DIR}/${FILE_NAME}.spv)
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPIRV}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
  )

  list(APPEND SPIRV_FILES ${SPIRV})
endforeach()

# Ray tracing shaders need Vulkan 1.2 / SPIR-V 1.4
foreach(SHADER ${RT_SHADERS})
  get_filename_component(FILE_NAME ${SHADER} NAME_WLE)
  set(SPIRV ${CMAKE_CURRENT_BINARY_DIR}/${FILE_NAME}.spv)
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V --target-env vulkan1.2 ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPIRV}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
    COMMENT ${SHADER}
  )

  list(APPEND SPIRV_FILES ${SPIRV})
endforeach()

source_group("Shader Files" FILES ${SHADERS_WITH_INCLUDES} ${SHADERS} ${RT_SHADERS})
source_group("Shader Includes" FILES ${SHADER_INCLUDES})

add_custom_target(vulkan-shaders DEPENDS ${SPIRV_FILES}
  SOURCES ${SHADERS_WITH_INCLUDES} ${SHADERS} ${RT_SHADERS} ${SHADER_INCLUDES})
