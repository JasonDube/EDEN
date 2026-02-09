# EDEN Engine - Getting Started

A minimal Vulkan game engine with a high-level API.

## Requirements

- C++17 compiler
- CMake 3.16+
- Vulkan SDK
- GLFW3
- GLM

Install on Ubuntu/Debian:
```bash
sudo apt install cmake build-essential libvulkan-dev vulkan-validationlayers glslc libglfw3-dev libglm-dev
```

## Building the Library

```bash
cd EDEN
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

This produces `libeden.a` static library.

## Quick Start

```cpp
#include <eden/Eden.hpp>

int main() {
    eden::Core engine;
    engine.init({
        .title = "My Game",
        .width = 800,
        .height = 600
    });

    auto triangle = engine.createMesh({
        .vertices = {{0, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}},
        .colors = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}
    });

    engine.run([&](float dt) {
        triangle->rotate(dt * 90.0f);
    });

    return 0;
}
```

## API Reference

### Core

The main engine class. Manages window, rendering, and scene.

```cpp
eden::Core engine;

// Initialize with config
engine.init({.title = "App", .width = 800, .height = 600});

// Create meshes (automatically added to scene)
auto mesh = engine.createMesh({...});

// Run main loop with update callback
engine.run([](float deltaTime) {
    // Your game logic here
});

// Or manual loop
while (engine.isRunning()) {
    engine.update(deltaTime);
    engine.render();
}
```

### Mesh

Geometry with transform. Created via `engine.createMesh()`.

```cpp
// Create mesh
auto mesh = engine.createMesh({
    .vertices = {{x, y}, ...},      // 2D positions
    .colors = {{r, g, b}, ...},     // RGB colors per vertex
    .indices = {0, 1, 2, ...}       // Optional index buffer
});

// Transform
mesh->setPosition(x, y, z);
mesh->rotate(degrees);              // Around Z axis
mesh->rotate(degrees, {0, 1, 0});   // Around custom axis
mesh->setScale(uniform);
mesh->setScale({x, y, z});

// Get transform matrix
glm::mat4 model = mesh->getModelMatrix();
```

### Transform

Position, rotation, scale component. Accessed via `mesh->getTransform()`.

```cpp
Transform& t = mesh->getTransform();

t.setPosition({x, y, z});
t.translate({dx, dy, dz});
t.rotate(degrees, axis);
t.setRotation(degrees, axis);
t.setScale(uniform);
t.scale(factor);

glm::mat4 matrix = t.getMatrix();
```

### Scene

Collection of meshes. Accessed via `engine.getScene()`.

```cpp
Scene& scene = engine.getScene();

scene.add(mesh);
scene.remove(mesh);
scene.clear();

for (auto& m : scene.getMeshes()) {
    // iterate meshes
}
```

## Linking in Your Project

### CMake

```cmake
add_executable(my_game main.cpp)

target_include_directories(my_game PRIVATE /path/to/EDEN/include)
target_link_libraries(my_game PRIVATE
    /path/to/EDEN/build/libeden.a
    vulkan glfw glm
)

# Copy shaders to your build directory
file(COPY /path/to/EDEN/build/shaders DESTINATION ${CMAKE_BINARY_DIR})
```

### Manual Compilation

```bash
g++ -std=c++17 main.cpp \
    -I/path/to/EDEN/include \
    -L/path/to/EDEN/build -leden \
    -lvulkan -lglfw -lm \
    -o my_game
```

## Project Structure

```
EDEN/
├── include/eden/     # Public headers
│   ├── Eden.hpp      # Main include (includes all)
│   ├── Core.hpp      # Engine facade
│   ├── Mesh.hpp      # Mesh + vertex data
│   ├── Transform.hpp # Position/rotation/scale
│   ├── Scene.hpp     # Object collection
│   └── Window.hpp    # Window interface
├── src/              # Implementation (internal)
├── shaders/          # GLSL shaders
├── examples/         # Example projects
└── docs/             # Documentation
```
