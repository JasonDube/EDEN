# EDEN Terrain Editor - Architecture Guide

## Overview

EDEN is a Vulkan-based terrain editor built in C++17. It features chunked terrain with LOD, procedural skybox, water rendering, model importing, and various terrain sculpting tools.

## Directory Structure

```
EDEN/
├── include/eden/          # Public API headers
│   ├── Terrain.hpp        # Terrain system (chunks, heightmaps)
│   ├── Camera.hpp         # FPS camera with fly/walk modes
│   ├── Core.hpp           # Engine core
│   └── SkyParameters.hpp  # Procedural sky configuration
├── src/
│   ├── Renderer/          # Vulkan rendering subsystems
│   │   ├── VulkanContext  # Device, queues, shared helpers
│   │   ├── Swapchain      # Swapchain management
│   │   ├── TerrainPipeline
│   │   ├── WaterRenderer
│   │   ├── ModelRenderer
│   │   ├── ProceduralSkybox
│   │   ├── SplineRenderer
│   │   ├── BrushRing
│   │   └── GizmoRenderer
│   └── Editor/            # Editor-specific tools
│       ├── EditorUI       # ImGui interface
│       ├── TerrainBrushTool
│       ├── ChunkManager
│       ├── PathTool
│       ├── Gizmo
│       └── GLBLoader
├── shaders/               # GLSL shaders (compiled to SPIR-V)
└── examples/terrain_editor/
    └── main.cpp           # Application entry point
```

## Core Design Principles

### 1. VulkanContext as Central Helper
All Vulkan boilerplate lives in `VulkanContext`. Renderers use shared methods:
```cpp
m_context.readFile("shaders/myshader.vert.spv");
m_context.createShaderModule(code);
m_context.createBuffer(size, usage, properties, buffer, memory);
m_context.findMemoryType(typeFilter, properties);
m_context.beginSingleTimeCommands();
m_context.endSingleTimeCommands(cmd);
```

### 2. Renderer Pattern
All renderers follow a consistent structure:
```cpp
class MyRenderer {
public:
    MyRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~MyRenderer();
    void render(VkCommandBuffer cmd, const glm::mat4& viewProj, ...);
    void recreatePipeline(VkRenderPass, VkExtent2D);  // For swapchain resize
private:
    void createPipeline(VkRenderPass, VkExtent2D);
    VulkanContext& m_context;
    VkPipeline m_pipeline;
    VkPipelineLayout m_pipelineLayout;
};
```

### 3. Terrain System
- **Chunked**: World divided into 64x64 vertex chunks
- **Per-vertex data**: Height, color, texture weights, texture indices, selection
- **ChunkManager**: Handles loading/unloading based on camera position
- **Brush tools**: Operate on terrain via `TerrainBrushTool`

### 4. Editor UI
- Uses Dear ImGui
- Callbacks connect UI to application logic
- State stored in `EditorUI`, callbacks defined in `main.cpp`

## Adding a New Renderer

1. **Create header** `src/Renderer/MyRenderer.hpp`:
```cpp
class MyRenderer {
public:
    MyRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~MyRenderer();
    void render(VkCommandBuffer cmd, const glm::mat4& viewProj);
    void recreatePipeline(VkRenderPass, VkExtent2D);
private:
    void createPipeline(VkRenderPass, VkExtent2D);
    VulkanContext& m_context;
    // ... pipeline, buffers, etc.
};
```

2. **Create implementation** `src/Renderer/MyRenderer.cpp`:
   - Use `m_context.readFile()` and `m_context.createShaderModule()` for shaders
   - Use `m_context.createBuffer()` for vertex/index buffers

3. **Create shaders** `shaders/myrenderer.vert` and `shaders/myrenderer.frag`

4. **Update CMakeLists.txt**:
   - Add `src/Renderer/MyRenderer.cpp` to `EDEN_SOURCES`
   - Add shaders to `compile_shaders()` call
   - Add `.spv` files to the copy command

5. **Update terrain_editor/CMakeLists.txt**:
   - Add shader `.spv` files to the copy command

6. **Integrate in main.cpp**:
   - Include header
   - Add `std::unique_ptr<MyRenderer>` member
   - Initialize in `init()`
   - Call `render()` in `recordCommandBuffer()`
   - Call `recreatePipeline()` in `recreateSwapchain()`
   - Reset in `cleanup()`

## Adding a New Brush Mode

1. Add enum value to `BrushMode` in `include/eden/Terrain.hpp`
2. Add UI string in `EditorUI::renderMainWindow()`
3. Implement logic in `TerrainBrushTool::apply()`

## Key Files Reference

| File | Purpose |
|------|---------|
| `VulkanContext.cpp` | Vulkan setup, shared helpers |
| `TerrainPipeline.cpp` | Terrain rendering pipeline |
| `WaterRenderer.cpp` | Water plane with animated waves |
| `TerrainBrushTool.cpp` | All brush implementations |
| `EditorUI.cpp` | ImGui interface |
| `main.cpp` | Application loop, integration |

## Build System

```bash
cd build
cmake ..
make -j4
./examples/terrain_editor/terrain_editor
```

Shaders are compiled with `glslc` (Vulkan SDK) via CMake's `compile_shaders()` function.

## Debug Features

- **Validation layers**: Enabled automatically in Debug builds (`EDEN_DEBUG` flag)
- **RenderDoc**: Works out of the box - launch app through RenderDoc to capture frames
- **Tracy**: Not integrated yet, but could be added for CPU profiling

## Future Considerations

When adding major systems (vegetation, physics, networking), consider:
- Extracting `main.cpp` into smaller manager classes
- Adding a base `IRenderer` interface if polymorphism is needed
- Creating an event system to replace EditorUI callbacks
- Adding resource pooling for textures/buffers if memory becomes an issue
