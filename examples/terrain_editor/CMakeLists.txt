add_executable(terrain_editor main.cpp grove_host.cpp MCPServer.cpp
    ${CMAKE_SOURCE_DIR}/src/Terminal/EdenTerminal.cpp)

# libvterm for terminal emulation
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED vterm)

target_link_libraries(terrain_editor PRIVATE eden ${VTERM_LIBRARIES} util)

# Grove scripting language (Rust static library)
add_library(grove STATIC IMPORTED)
set_target_properties(grove PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/modules/eden_script/grove/target/release/libgrove.a
)
target_link_libraries(terrain_editor PRIVATE grove dl m)
target_compile_definitions(terrain_editor PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Need access to internal headers for TerrainPipeline
target_include_directories(terrain_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/modules/eden_script/grove/include
    ${VTERM_INCLUDE_DIRS}
)

# Copy shaders to example build directory
add_custom_command(TARGET terrain_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:terrain_editor>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/default.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/terrain.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/terrain.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/brush_ring.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/brush_ring.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox_procedural.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox_procedural.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/gizmo.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/gizmo.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/water.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/water.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.frag.spv
        $<TARGET_FILE_DIR:terrain_editor>/shaders/
)

# Copy sounds to example build directory
add_custom_command(TARGET terrain_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:terrain_editor>/sounds
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/sounds
        $<TARGET_FILE_DIR:terrain_editor>/sounds
)

# Copy splash image to build directory
add_custom_command(TARGET terrain_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/eden_splash.jpg
        $<TARGET_FILE_DIR:terrain_editor>/eden_splash.jpg
)

# Copy terrain textures to build directory
add_custom_command(TARGET terrain_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:terrain_editor>/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
        $<TARGET_FILE_DIR:terrain_editor>/textures
)

# Copy building textures to build directory (if they exist)
add_custom_command(TARGET terrain_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:terrain_editor>/textures/building
)
if(EXISTS "${CMAKE_SOURCE_DIR}/textures/building")
    add_custom_command(TARGET terrain_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/textures/building
            $<TARGET_FILE_DIR:terrain_editor>/textures/building
    )
endif()

# Copy Grove logo to build directory
add_custom_command(TARGET terrain_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/grove_logo.png
        $<TARGET_FILE_DIR:terrain_editor>/grove_logo.png
)

# Copy Grove scripts to build directory
add_custom_command(TARGET terrain_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:terrain_editor>/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:terrain_editor>/scripts
)
