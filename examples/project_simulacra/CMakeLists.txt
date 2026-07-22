add_executable(project_simulacra main.cpp)

target_link_libraries(project_simulacra PRIVATE eden)

# Renderer headers live under src/, not include/ (same as slag_legion).
target_include_directories(project_simulacra PRIVATE ${CMAKE_SOURCE_DIR}/src)

# So the in-game Servers panel can launch the Python dialogue backend from the
# source tree regardless of the game's working directory. httplib + nlohmann/json
# + pthread come transitively from the eden target.
target_compile_definitions(project_simulacra PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# libmpv drives scene/portrait video playback (software render -> ImGui texture).
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
target_link_libraries(project_simulacra PRIVATE PkgConfig::MPV)

# Symlink the source assets/ next to the binary so it resolves "assets/..." at
# runtime without copying (grows as the game gains content).
add_custom_command(TARGET project_simulacra POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:project_simulacra>/assets
)
