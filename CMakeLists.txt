cmake_minimum_required(VERSION 3.16)
project(EDEN VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Fetch Dear ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# Fetch stb (for image loading)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# Fetch tinygltf (for GLB/GLTF model loading)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.8.21
)
FetchContent_MakeAvailable(tinygltf)

# Fetch nlohmann/json (for level serialization)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Fetch NFD-Extended (native file dialogs with portal support)
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.1.1
)
# Use portal backend on Linux for Wayland/Cosmic compatibility (requires libdbus-1-dev)
set(NFD_PORTAL ON CACHE BOOL "Use xdg-desktop-portal on Linux")
FetchContent_MakeAvailable(nfd)

# Fetch cpp-httplib (header-only HTTP client/server)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
# Disable OpenSSL - we only need local HTTP
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "")
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "")
FetchContent_MakeAvailable(httplib)

# Fetch miniaudio (single-header audio library)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
)
FetchContent_MakeAvailable(miniaudio)

# Fetch Bullet Physics (collision detection)
FetchContent_Declare(
    bullet3
    GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
    GIT_TAG 3.25
)
# Configure Bullet to build only what we need
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(bullet3)

# Fetch Jolt Physics (modern character controller)
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.2.0
    SOURCE_SUBDIR Build
)
# Configure Jolt
set(INTERPROCEDURAL_OPTIMIZATION ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

# Create ImGui library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

# Include shader compilation
include(cmake/CompileShaders.cmake)

# EDEN Core Library sources
set(EDEN_SOURCES
    src/Core.cpp
    src/Window.cpp
    src/Mesh.cpp
    src/Camera.cpp
    src/Input.cpp
    src/Terrain.cpp
    src/Renderer/VulkanContext.cpp
    src/Renderer/Swapchain.cpp
    src/Renderer/Pipeline.cpp
    src/Renderer/Buffer.cpp
    src/Renderer/RenderSystem.cpp
    src/Renderer/TerrainPipeline.cpp
    src/Renderer/TextureManager.cpp
    src/Renderer/Skybox.cpp
    src/Renderer/ProceduralSkybox.cpp
    src/Renderer/BrushRing.cpp
    src/Renderer/GizmoRenderer.cpp
    src/Renderer/ModelRenderer.cpp
    src/Renderer/SplineRenderer.cpp
    src/Renderer/WaterRenderer.cpp
    src/Renderer/VulkanApplicationBase.cpp
    src/Renderer/ImGuiManager.cpp
    src/Renderer/PipelineBuilder.cpp
    src/Editor/EditorUI.cpp
    src/Editor/TerrainBrushTool.cpp
    src/Editor/ChunkManager.cpp
    src/Editor/Gizmo.cpp
    src/Editor/GLBLoader.cpp
    src/Editor/LimeLoader.cpp
    src/Editor/SceneObject.cpp
    src/Editor/PathTool.cpp
    src/Entity/Entity.cpp
    src/Entity/Action.cpp
    src/Entity/ActionSystem.cpp
    src/Editor/LevelSerializer.cpp
    src/Editor/BinaryLevelWriter.cpp
    src/Editor/BinaryLevelReader.cpp
    src/Editor/PrimitiveMeshBuilder.cpp
    src/Editor/AINode.cpp
    src/Editor/AIPath.cpp
    src/Renderer/AINodeRenderer.cpp
    src/Economy/EconomySystem.cpp
    src/City/CityGovernor.cpp
    src/AI/AStarPathfinder.cpp
    src/AI/TraderAI.cpp
    src/AI/DogfightAI.cpp
    src/Renderer/DialogueBubbleRenderer.cpp
    src/Game/SentientBeing.cpp
    src/Game/Human.cpp
    src/Game/Clone.cpp
    src/Game/Robot.cpp
    src/Game/Android.cpp
    src/Game/Cyborg.cpp
    src/Game/Alien.cpp
    src/Network/AsyncHttpClient.cpp
    src/Audio/Audio.cpp
    src/Animation/AnimationPlayer.cpp
    src/Renderer/SkinnedModelRenderer.cpp
    src/Editor/SkinnedGLBLoader.cpp
    src/Physics/PhysicsWorld.cpp
    src/Physics/JoltCharacter.cpp
    src/Physics/HomebrewCharacter.cpp
    src/GameModules/GameModule.cpp
    src/Zone/ZoneSystem.cpp
)

# Create static library
add_library(eden STATIC ${EDEN_SOURCES})

# Include directories
target_include_directories(eden PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
    ${tinygltf_SOURCE_DIR}
    ${httplib_SOURCE_DIR}
    ${miniaudio_SOURCE_DIR}
    ${bullet3_SOURCE_DIR}/src
    ${JoltPhysics_SOURCE_DIR}/..
)
target_include_directories(eden PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(eden PUBLIC
    Vulkan::Vulkan
    glfw
    glm::glm
    imgui
    nlohmann_json::nlohmann_json
    nfd
    pthread
    BulletDynamics
    BulletCollision
    LinearMath
    Jolt
)

# Enable validation layers in debug builds
target_compile_definitions(eden PUBLIC
    $<$<CONFIG:Debug>:EDEN_DEBUG>
)

# Compile shaders
compile_shaders(eden
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/default.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/default.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/brush_ring.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/brush_ring.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox_procedural.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox_procedural.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/model.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/model.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/wireframe.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/wireframe.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned_model.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned_model.frag
)

# Copy compiled shaders to build directory
add_custom_command(TARGET eden POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:eden>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/default.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/default.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/terrain.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/terrain.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/skybox.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/skybox.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/brush_ring.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/brush_ring.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/skybox_procedural.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/skybox_procedural.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/gizmo.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/gizmo.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/model.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/model.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/wireframe.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/wireframe.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/selection.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/selection.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/water.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/water.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/skinned_model.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/skinned_model.frag.spv
        $<TARGET_FILE_DIR:eden>/shaders/
)

# Build modules
add_subdirectory(modules/ai_companion)

# Build examples
add_subdirectory(examples/spinning_triangle)
add_subdirectory(examples/terrain_flyover)
add_subdirectory(examples/terrain_editor)
add_subdirectory(examples/model_editor)
add_subdirectory(examples/eve)
add_subdirectory(examples/spacegame)
