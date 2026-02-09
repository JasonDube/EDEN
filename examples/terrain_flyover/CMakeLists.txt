add_executable(terrain_flyover main.cpp)

target_link_libraries(terrain_flyover PRIVATE eden)

# Need access to internal headers for TerrainPipeline
target_include_directories(terrain_flyover PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Copy shaders to example build directory
add_custom_command(TARGET terrain_flyover POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:terrain_flyover>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/default.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/terrain.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/terrain.frag.spv
        $<TARGET_FILE_DIR:terrain_flyover>/shaders/
)
