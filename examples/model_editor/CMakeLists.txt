add_executable(model_editor
    main.cpp
    EditableMesh.cpp
    AnimationMode.cpp
    ModelingMode.cpp
    ModelingMode_UV.cpp
    ModelingMode_Gizmo.cpp
    ModelingMode_Paint.cpp
    ModelingMode_Snap.cpp
    ModelingMode_Retopo.cpp
    ModelingMode_AutoRetopo.cpp
    ModelingMode_Primitives.cpp
    ModelingMode_PathTube.cpp
    ModelingMode_Slice.cpp
    ModelingMode_Rigging.cpp
    PaintBrushRing.cpp
    MCPServer.cpp
)

target_link_libraries(model_editor PRIVATE eden httplib::httplib)

# Need access to internal headers for renderers and editor components
target_include_directories(model_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Copy shaders to example build directory
add_custom_command(TARGET model_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:model_editor>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/default.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/gizmo.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/gizmo.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/brush_ring.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/brush_ring.frag.spv
        $<TARGET_FILE_DIR:model_editor>/shaders/
)
