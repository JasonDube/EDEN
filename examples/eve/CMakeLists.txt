add_executable(eve
    main.cpp
    Eve.cpp
    Spaceship.cpp
    ChatInterface.cpp
    ParameterLab.cpp
    XenkTerminal.cpp
    ShipInterior.cpp
)

target_link_libraries(eve PRIVATE eden httplib::httplib)

# Need access to internal headers for renderers and network
target_include_directories(eve PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Copy shaders to example build directory
add_custom_command(TARGET eve POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:eve>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/default.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/skinned_model.frag.spv
        $<TARGET_FILE_DIR:eve>/shaders/
)

# Copy Eve's personality configuration
add_custom_command(TARGET eve POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/eve_personality.json
        $<TARGET_FILE_DIR:eve>/
)
