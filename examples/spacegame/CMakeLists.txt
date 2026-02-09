add_executable(spacegame
    main.cpp
    GameUI.cpp
)

target_link_libraries(spacegame PRIVATE eden)

# Need access to internal headers
target_include_directories(spacegame PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Copy shaders to build directory
add_custom_command(TARGET spacegame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:spacegame>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/default.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox_procedural.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skybox_procedural.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.frag.spv
        $<TARGET_FILE_DIR:spacegame>/shaders/
)

# Copy game-specific assets (if any)
# add_custom_command(TARGET spacegame POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         ${CMAKE_CURRENT_SOURCE_DIR}/assets/personality.json
#         $<TARGET_FILE_DIR:spacegame>/personality.json
# )
