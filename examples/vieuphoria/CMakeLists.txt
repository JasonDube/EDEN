add_executable(vieuphoria main.cpp hud.cpp)

find_package(Threads REQUIRED)
target_link_libraries(vieuphoria PRIVATE eden m Threads::Threads) # m: stb_truetype; Threads: httplib

# Internal EDEN headers (Renderer/*) under src/, plus httplib + nlohmann/json
# (fetched into _deps by the top-level build) for the ai_companion backend call.
target_include_directories(vieuphoria PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/_deps/httplib-src
    ${CMAKE_SOURCE_DIR}/_deps/json-src/single_include)

# ModelRenderer loads shader SPIR-V from ./shaders relative to the CWD.
add_custom_command(TARGET vieuphoria POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:vieuphoria>/shaders)
