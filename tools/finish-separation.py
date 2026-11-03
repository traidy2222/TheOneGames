from pathlib import Path
import json
import shutil

root = Path(__file__).resolve().parents[1]
if (root / 'migration-backup/completed').exists():
    raise SystemExit('Migration already complete; do not replay it over later changes.')
engine = root.parent / 'TheOne'
backup = root / 'migration-backup/source'
cmake = (engine / 'CMakeLists.txt').read_text()
cmake = cmake[:cmake.index('add_library(one_content')]
cmake += '''
# This repository owns reusable engine services and tools only.
add_library(one_content STATIC src/content.cpp src/physics.cpp)
target_include_directories(one_content PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE ${cgltf_SOURCE_DIR} ${stb_SOURCE_DIR})
target_compile_definitions(one_content PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(one_content PUBLIC nlohmann_json::nlohmann_json Jolt glm::glm)
add_library(TheOne::Content ALIAS one_content)
add_library(one_ui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(one_ui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(one_ui PUBLIC SDL3::SDL3-static Vulkan::Vulkan)
add_library(one_renderer STATIC src/renderer.cpp)
target_include_directories(one_renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(one_renderer PUBLIC one_ui glm::glm)
add_library(TheOne::Renderer ALIAS one_renderer)
foreach(SHADER scene.vert scene.frag shadow.vert)
  set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv")
  add_custom_command(OUTPUT "${OUTPUT}" COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" -o "${OUTPUT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" VERBATIM)
  list(APPEND SHADERS "${OUTPUT}")
endforeach()
add_custom_target(theone_shaders DEPENDS ${SHADERS})
set_property(GLOBAL PROPERTY THEONE_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
function(theone_copy_shaders TARGET)
  get_property(shader_directory GLOBAL PROPERTY THEONE_SHADER_DIRECTORY)
  add_dependencies(${TARGET} theone_shaders)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${shader_directory}" "$<TARGET_FILE_DIR:${TARGET}>/shaders" VERBATIM)
endfunction()
if(MSVC)
  target_compile_options(one_content PRIVATE /W4 /permissive- /EHsc)
endif()
option(THEONE_BUILD_TOOLS "Build generic asset conversion tools" ${PROJECT_IS_TOP_LEVEL})
option(THEONE_BUILD_TESTS "Build engine-only regression tests" ${PROJECT_IS_TOP_LEVEL})
if(THEONE_BUILD_TOOLS)
  add_executable(one_inspect tools/inspect_asset.cpp)
  target_link_libraries(one_inspect PRIVATE TheOne::Content)
  add_executable(one_import tools/import_asset.cpp)
  target_link_libraries(one_import PRIVATE TheOne::Content assimp::assimp)
endif()
if(THEONE_BUILD_TESTS)
  enable_testing()
  add_executable(theone_core_tests tests/core_tests.cpp)
  target_link_libraries(theone_core_tests PRIVATE TheOne::Content)
  add_test(NAME engine_core COMMAND theone_core_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/assets")
  if(THEONE_BUILD_TOOLS)
    add_test(NAME engine_convert_obj COMMAND ${CMAKE_COMMAND}
      -DIMPORTER=$<TARGET_FILE:one_import> -DINSPECTOR=$<TARGET_FILE:one_inspect>
      -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/assets/conversion/cube.obj -DDEST=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/conversion_test.cmake)
  endif()
endif()
'''
(engine / 'CMakeLists.txt').write_text(cmake)
tests = (backup / 'tests/core_tests.cpp').read_text().replace('#include "game.hpp"\n', '')
start = tests.index('        one::DoubleJump jump;')
end = tests.index('        // Walk into', start)
tests = tests[:start] + tests[end:]
start = tests.index('        int sounds=0;')
end = tests.index('        std::cout', start)
tests = tests[:start] + tests[end:]
tests = tests.replace(', game events', '')
(engine / 'tests/core_tests.cpp').write_text(tests)
(root / 'tests').mkdir(exist_ok=True)
shutil.copy2(root / 'migration-backup/original-core-tests.cpp', root / 'tests/game_integration.cpp')
(root / 'games/robots/src').mkdir(exist_ok=True)
main = (backup / 'src/main.cpp').read_text()
(root / 'games/robots/src/main.cpp').write_text(main.replace('Options options;', 'Options options; options.robots=true;'))
(root / 'games/sandbox/src/main.cpp').write_text(main.replace('            else if(arg=="--robots") options.robots=true;\n', ''))

# Rewrite structured content references, including saved scenes, without touching
# binary game data or historical research reports/hashes.
mapping = [
    ((engine / 'build').as_posix(), (root / 'games/robots/build').as_posix()),
    ((engine / 'assets').as_posix(), (root / 'games/sandbox/assets').as_posix()),
]
changed = 0
def relocate(value):
    if isinstance(value, str):
        path = value.replace('\\', '/')
        for old, new in mapping:
            if path.lower().startswith(old.lower() + '/'):
                return new + path[len(old):]
        return value
    if isinstance(value, list):
        return [relocate(v) for v in value]
    if isinstance(value, dict):
        return {k: relocate(v) for k, v in value.items()}
    return value
for base in [root / 'games/robots/build', root / 'games/sandbox/saves']:
    if not base.exists(): continue
    for path in base.rglob('*'):
        if path.suffix.lower() not in ('.one', '.gltf'): continue
        try:
            data = json.loads(path.read_text(encoding='utf-8-sig'))
        except (json.JSONDecodeError, UnicodeDecodeError):
            print(f'Preserved non-JSON historical artifact unchanged: {path}')
            continue
        updated = relocate(data)
        if data != updated:
            path.write_text(json.dumps(updated, indent=2))
            changed += 1
print(f'Engine targets separated; relocated references in {changed} scene/asset files.')
