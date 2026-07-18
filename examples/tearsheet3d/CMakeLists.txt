# ── wlroots / Wayland server deps for the embedded compositor host ───────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(WLROOTS   REQUIRED wlroots)
pkg_check_modules(WLSERVER  REQUIRED wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(TSPIXMAN  REQUIRED pixman-1)
pkg_check_modules(TSLIBDRM  REQUIRED libdrm)

# Generate the xdg-shell server protocol header via wayland-scanner.
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
set(TS_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${TS_PROTO_DIR})
set(XDG_SHELL_HEADER ${TS_PROTO_DIR}/xdg-shell-protocol.h)
add_custom_command(
    OUTPUT  ${XDG_SHELL_HEADER}
    COMMAND ${WAYLAND_SCANNER} server-header ${XDG_SHELL_XML} ${XDG_SHELL_HEADER}
    DEPENDS ${XDG_SHELL_XML}
    VERBATIM)
add_custom_target(tearsheet3d-protocols DEPENDS ${XDG_SHELL_HEADER})

add_executable(tearsheet3d main.cpp wayland_host.cpp)
add_dependencies(tearsheet3d tearsheet3d-protocols)

target_link_libraries(tearsheet3d PRIVATE eden
    ${WLROOTS_LIBRARIES} ${WLSERVER_LIBRARIES} ${XKBCOMMON_LIBRARIES}
    ${TSPIXMAN_LIBRARIES} ${TSLIBDRM_LIBRARIES})

# wlroots public headers require this opt-in for unstable API.
target_compile_definitions(tearsheet3d PRIVATE WLR_USE_UNSTABLE)

# Internal EDEN renderer headers (Renderer/*, Editor/*) live under src/,
# plus wlroots headers and the generated protocol header.
target_include_directories(tearsheet3d PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${TS_PROTO_DIR}
    ${WLROOTS_INCLUDE_DIRS} ${WLSERVER_INCLUDE_DIRS} ${XKBCOMMON_INCLUDE_DIRS}
    ${TSPIXMAN_INCLUDE_DIRS} ${TSLIBDRM_INCLUDE_DIRS})

target_link_directories(tearsheet3d PRIVATE ${WLROOTS_LIBRARY_DIRS})

# ModelRenderer loads shader SPIR-V from ./shaders relative to the CWD, so copy
# the whole compiled shader set next to the binary.
add_custom_command(TARGET tearsheet3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:tearsheet3d>/shaders)
