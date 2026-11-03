add_executable(tornado src/main.cpp src/simulation.cpp)
target_link_libraries(tornado PRIVATE TheOne::Renderer)
target_include_directories(tornado PRIVATE src)
set_target_properties(tornado PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tornado")
theone_copy_shaders(tornado)
set(TORNADO_SHADERS)
foreach(STAGE vert frag)
  set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/scene.${STAGE}.spv")
  add_custom_command(OUTPUT "${OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.${STAGE}" -o "${OUTPUT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.${STAGE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/vortex_field.shared" VERBATIM)
  list(APPEND TORNADO_SHADERS "${OUTPUT}")
endforeach()
add_custom_target(tornado_volume_shaders
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TORNADO_SHADERS} "$<TARGET_FILE_DIR:tornado>/shaders"
  DEPENDS ${TORNADO_SHADERS} VERBATIM)
add_dependencies(tornado_volume_shaders tornado_shaders)
add_dependencies(tornado tornado_volume_shaders)
add_executable(tornado_model_check tests/model_check.cpp src/simulation.cpp)
target_include_directories(tornado_model_check PRIVATE src)
target_link_libraries(tornado_model_check PRIVATE TheOne::Core)
add_test(NAME tornado_model COMMAND tornado_model_check)
