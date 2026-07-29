add_executable(tessara_axiom
    main.cpp
    Heightfield.cpp
    Walker.cpp
    CreatureMesh.cpp
    Biped.cpp
    BipedMesh.cpp
    ScenePipeline.cpp
    Conversation.cpp
    Ship.cpp
    Ground.cpp
)

target_link_libraries(tessara_axiom PRIVATE eden)

# The gait's two knobs are not guessable by eye, so they get measured instead --
# and neither is whether anything can walk up the ramp, so Checks.cpp rides along
# in the same binary. No Vulkan, no window: none of the creatures, the ground or
# the ship have a renderer in them, which is exactly why they can be tested.
add_executable(tessara_gait_sim
    gait_sim.cpp
    Checks.cpp
    Heightfield.cpp
    Ground.cpp
    Walker.cpp
    Biped.cpp
    Ship.cpp
)
target_link_libraries(tessara_gait_sim PRIVATE eden)
target_include_directories(tessara_gait_sim PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Renderer headers (VulkanApplicationBase, ImGuiManager, ...) live under src/,
# not include/.
target_include_directories(tessara_axiom PRIVATE ${CMAKE_SOURCE_DIR}/src)

# The head model lives beside the binary, like every other example's assets.
add_custom_command(TARGET tessara_axiom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:tessara_axiom>/assets
)

# The scene pipeline loads these at startup, relative to the working directory.
add_custom_command(TARGET tessara_axiom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tessara_axiom>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/tessara.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/tessara.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model_instanced.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/grass.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.frag.spv
        $<TARGET_FILE_DIR:tessara_axiom>/shaders/
)
