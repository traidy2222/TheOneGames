add_executable(effects_lab src/main.cpp)
target_link_libraries(effects_lab PRIVATE TheOne::Renderer TheOne::Volumes)
set_target_properties(effects_lab PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/effects-lab")
theone_copy_shaders(effects_lab)
