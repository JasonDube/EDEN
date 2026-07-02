add_executable(tabletop main.cpp)

target_link_libraries(tabletop PRIVATE eden)

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
        $<TARGET_FILE_DIR:tabletop>/shaders/
)

# Symlink the source assets/ next to the binary so it resolves "assets/..." at
# runtime without copying (the GLB character models are large).
add_custom_command(TARGET tabletop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:tabletop>/assets
)
