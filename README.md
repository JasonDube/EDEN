# EDEN

A 3D world simulation engine with terrain editing, model editing, AI-driven NPCs, and a custom scripting language (Grove).

## System Requirements

- Linux (tested on Ubuntu/Pop!_OS)
- Vulkan-capable GPU with drivers installed
- CMake 3.16+
- C++17 compiler (GCC 10+ or Clang 12+)
- Rust 1.70+ (for Grove scripting language)
- Python 3.10+ (optional, for AI backend)

## Install Dependencies

```bash
# Ubuntu/Debian/Pop!_OS
sudo apt install cmake build-essential libvulkan-dev vulkan-validationlayers \
  glslc libglfw3-dev libglm-dev libdbus-1-dev
```

```bash
# Install Rust (if not already installed)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

## Build

```bash
# 1. Build Grove scripting engine (Rust — must be done first)
cd modules/eden_script/grove
cargo build --release
cd ../../..

# 2. Build EDEN (C++ — downloads dependencies automatically via CMake FetchContent)
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

Build outputs:
- `build/examples/terrain_editor/terrain_editor`
- `build/examples/model_editor/model_editor`

## AI Backend (Optional)

The AI backend provides LLM-powered NPCs via a FastAPI server.

```bash
cd backend
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

Create `backend/.env` with your API keys:
```
XAI_API_KEY=your-key-here
GROK_MODEL=grok-3
OLLAMA_URL=http://localhost:11434
OLLAMA_MODEL=dolphin-mixtral:8x7b
DEFAULT_PROVIDER=grok
```

Start the server:
```bash
python server.py
```

## Project Structure

```
src/              Engine source (Editor, Renderer, Physics, AI, Economy, Zone)
include/          Public headers (Action, Camera, Terrain, etc.)
shaders/          GLSL shader sources (compiled to SPIR-V during build)
examples/         Applications (terrain_editor, model_editor)
modules/          Grove scripting language (Rust crate)
backend/          AI backend server (Python/FastAPI)
cmake/            CMake helper modules
```

## Key Features

- Vulkan-based terrain renderer with real-time editing
- Scene object system with GLB/GLTF model loading
- Grove scripting language with C FFI for in-game automation
- AI NPC system (Xenk architect, AlgoBots) with LLM integration
- Zone system with land ownership and economy
- Physics via Jolt + Bullet
- Skinned model animation
- Spline/path tools for AI navigation
