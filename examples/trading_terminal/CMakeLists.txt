add_executable(trading_terminal
    main.cpp
    MarketDataClient.cpp
)

target_link_libraries(trading_terminal PRIVATE eden)

# Need access to internal headers
target_include_directories(trading_terminal PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Copy shaders to build directory
add_custom_command(TARGET trading_terminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:trading_terminal>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/default.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/selection.frag.spv
        $<TARGET_FILE_DIR:trading_terminal>/shaders/
)
