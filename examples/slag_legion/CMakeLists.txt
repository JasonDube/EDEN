add_executable(slag_legion main.cpp)

target_link_libraries(slag_legion PRIVATE eden)

# Renderer headers live under src/, not include/ (same as the tabletop example).
target_include_directories(slag_legion PRIVATE ${CMAKE_SOURCE_DIR}/src)

# So the in-game Servers panel can launch the Python dialogue backend from the
# source tree regardless of the game's working directory. httplib + nlohmann/json
# + pthread come transitively from the eden target.
target_compile_definitions(slag_legion PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# libmpv drives scene video playback (software render -> ImGui texture).
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
target_link_libraries(slag_legion PRIVATE PkgConfig::MPV)

# Symlink the source assets/ next to the binary so it resolves "assets/..." at
# runtime without copying (grows as the game gains content).
add_custom_command(TARGET slag_legion POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:slag_legion>/assets
)

# The 3D flight mode uses the engine's ModelRenderer (+ optional skybox), which loads
# these compiled SPIR-V shaders from ./shaders/ at runtime. Copy them next to the binary.
add_custom_command(TARGET slag_legion POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:slag_legion>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model_instanced.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/grass.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox_procedural.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox_procedural.frag.spv
        $<TARGET_FILE_DIR:slag_legion>/shaders/
)
