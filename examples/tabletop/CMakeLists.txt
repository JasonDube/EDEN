add_executable(tabletop main.cpp)

target_link_libraries(tabletop PRIVATE eden)

# libmpv drives dialogue video playback (software render API -> ImGui texture),
# same approach as Tearsheet.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
target_link_libraries(tabletop PRIVATE PkgConfig::MPV)

# So the in-game Options menu can launch the Python dialogue backend from the
# source tree regardless of the game's working directory.
target_compile_definitions(tabletop PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Renderer/Editor headers live under src/, not include/.
target_include_directories(tabletop PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ModelRenderer builds all of these pipelines in its constructor, so every
# example that uses it must have these compiled shaders alongside the binary.
add_custom_command(TARGET tabletop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tabletop>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model_instanced.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/terrain.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/terrain.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/grass.frag.spv
        $<TARGET_FILE_DIR:tabletop>/shaders/
)

# Symlink the terrain splatmap textures next to the binary so the game loads the
# SAME texture set (and slot order) the levels were painted with in the editor.
add_custom_command(TARGET tabletop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_SOURCE_DIR}/examples/terrain_editor/assets/textures
        $<TARGET_FILE_DIR:tabletop>/terrain_textures
)

# Symlink the source assets/ next to the binary so it resolves "assets/..." at
# runtime without copying (the GLB character models are large).
add_custom_command(TARGET tabletop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:tabletop>/assets
)
