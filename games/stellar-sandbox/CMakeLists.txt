add_executable(stellar_sandbox src/main.cpp)
target_link_libraries(stellar_sandbox PRIVATE TheOne::Renderer TheOne::Lighting)
set_target_properties(stellar_sandbox PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/stellar-sandbox")
theone_copy_shaders(stellar_sandbox)
