add_executable(spinning_triangle main.cpp)

target_link_libraries(spinning_triangle PRIVATE eden)

# Copy shaders to example build directory
add_custom_command(TARGET spinning_triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:spinning_triangle>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/default.frag.spv
        $<TARGET_FILE_DIR:spinning_triangle>/shaders/
)
