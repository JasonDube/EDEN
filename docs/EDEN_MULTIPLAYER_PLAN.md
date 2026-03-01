# EDEN Multiplayer Implementation Plan

**Author:** Xenk (Architect AI)  
**Date:** 2026-02-05  
**Target:** EDEN Vulkan Engine Multiplayer + AI Agent Integration

---

## Executive Summary

This document outlines the architecture and implementation plan for adding multiplayer networking to EDEN, enabling:
1. Multiple human players to coexist in a shared 1024×1024 toroidal terrain world
2. AI agents (via gateway services like OpenClaw) to connect and interact alongside humans
3. Server-authoritative architecture to prevent cheating and maintain consistency

---

## 1. Current EDEN Architecture Analysis

### 1.1 Core Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `VulkanApplicationBase` | `src/Renderer/VulkanApplicationBase.hpp` | Main loop, frame timing, render orchestration |
| `Camera` | `include/eden/Camera.hpp` | Player position, movement modes (fly/walk), view matrices |
| `Entity` | `include/eden/Entity.hpp` | Generic game entity with Transform, flags, properties, behaviors |
| `Terrain` | `include/eden/Terrain.hpp` | 1024×1024 chunk-based toroidal terrain system |
| `ICharacterController` | `include/eden/ICharacterController.hpp` | Physics abstraction (Jolt/Homebrew backends) |
| `SceneObject` | `src/Editor/SceneObject.hpp` | Renderable objects with mesh/model handles |
| `AsyncHttpClient` | `src/Network/AsyncHttpClient.cpp` | Existing async HTTP (for AI backend) |

### 1.2 Update Loop (terrain_editor)

```
VulkanApplicationBase::run()
  └── mainLoop()
        └── while (!shouldClose)
              ├── update(deltaTime)      ← Game logic, input, physics
              │     ├── handleCameraInput()
              │     ├── m_actionSystem.update()
              │     ├── m_terrain.update()
              │     └── updatePlayMode() / updateEditorMode()
              │
              └── recordCommandBuffer()   ← Rendering
```

### 1.3 Key Observations

- **No existing networking** beyond `AsyncHttpClient` (HTTP-based, for AI chat)
- **Camera doubles as player** — position/velocity managed directly in Camera class
- **Physics decoupled** via `ICharacterController` interface (good for server-side physics)
- **Entity system exists** but underutilized in terrain_editor (mostly SceneObject-based)
- **Terrain already supports wrapping** (`wrapWorld = true` in `TerrainConfig`)
- **Fixed bounds pre-loading** available (`useFixedBounds = true`)

---

## 2. Networking Library Selection

### 2.1 Comparison

| Library | Protocol | NAT Punch | Encryption | Complexity | License |
|---------|----------|-----------|------------|------------|---------|
| **ENet** | UDP | Manual | No | Low | MIT |
| **GameNetworkingSockets** | UDP | Yes | Yes | Medium | BSD-3 |
| **RakNet** | UDP | Yes | Yes | High | BSD |
| **Boost.Asio + Beast** | TCP/WebSocket | No | Optional | Medium | BSL-1.0 |
| **Steamworks** | UDP | Yes | Yes | Medium | Proprietary |

### 2.2 Recommendation: **ENet**

**Rationale:**
- Minimal dependencies (single C library)
- Proven in games (Cube 2, Minecraft bedrock protocol inspired by it)
- Reliable + unreliable channels (perfect for movement + events)
- Easy CMake integration via FetchContent
- MIT license compatible with EDEN
- Can layer NAT punch-through later if needed

**CMake Addition:**
```cmake
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
)
FetchContent_MakeAvailable(enet)
```

---

## 3. Server/Client Architecture

### 3.1 High-Level Design

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           EDEN SERVER                                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────────┐ │
│  │ ENet Host   │  │ World State │  │  Physics    │  │  AI Gateway    │ │
│  │ (UDP:7777)  │  │  Manager    │  │  (Headless) │  │  (WS:7778)     │ │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └───────┬────────┘ │
│         │                │                │                  │          │
│         └────────────────┴────────────────┴──────────────────┘          │
│                                   │                                      │
│                          ┌────────▼────────┐                            │
│                          │  Authoritative  │                            │
│                          │   Game Loop     │                            │
│                          │  (60 tick/sec)  │                            │
│                          └─────────────────┘                            │
└─────────────────────────────────────────────────────────────────────────┘
                 ▲                    ▲                    ▲
                 │ ENet               │ ENet               │ WebSocket
                 │                    │                    │
        ┌────────┴────────┐  ┌───────┴────────┐   ┌───────┴────────┐
        │  EDEN Client 1  │  │  EDEN Client 2 │   │   AI Agent     │
        │  (Human + GPU)  │  │  (Human + GPU) │   │  (OpenClaw)    │
        └─────────────────┘  └────────────────┘   └────────────────┘
```

### 3.2 Server Responsibilities

1. **Authoritative world state** — canonical positions of all entities
2. **Input processing** — receive player inputs, validate, apply
3. **Physics simulation** — run `ICharacterController` server-side
4. **State broadcasting** — send snapshots/deltas to all clients
5. **AI Gateway bridge** — translate AI commands ↔ game actions

### 3.3 Client Responsibilities

1. **Render** — display world state received from server
2. **Capture input** — send movement/action intents to server
3. **Client-side prediction** — smooth movement while awaiting server confirmation
4. **Interpolation** — smooth rendering of other players between snapshots

### 3.4 AI Gateway Responsibilities

1. **WebSocket server** — AI agents connect via WS (simpler than raw UDP for bots)
2. **World description** — serialize visible world state as structured text/JSON
3. **Action translation** — convert AI commands ("move north", "interact with NPC") to game actions
4. **Rate limiting** — prevent AI spam (1-2 actions per second)

---

## 4. Network Protocol Design

### 4.1 Packet Types

```cpp
enum class PacketType : uint8_t {
    // Client → Server
    C2S_JOIN_REQUEST    = 0x01,  // Player wants to join
    C2S_INPUT           = 0x02,  // Movement/action input
    C2S_CHAT            = 0x03,  // Chat message
    C2S_INTERACT        = 0x04,  // Interact with entity
    
    // Server → Client
    S2C_JOIN_RESPONSE   = 0x10,  // Accept/reject + player ID + world seed
    S2C_PLAYER_SPAWN    = 0x11,  // New player joined
    S2C_PLAYER_LEAVE    = 0x12,  // Player disconnected
    S2C_WORLD_SNAPSHOT  = 0x13,  // Full world state (on join)
    S2C_STATE_DELTA     = 0x14,  // Incremental update (positions, etc.)
    S2C_ENTITY_SPAWN    = 0x15,  // New entity created
    S2C_ENTITY_DESTROY  = 0x16,  // Entity removed
    S2C_CHAT            = 0x17,  // Broadcast chat message
    
    // AI Gateway (via WebSocket, JSON-based)
    AI_PERCEPTION       = 0x20,  // World state for AI
    AI_ACTION           = 0x21,  // AI wants to perform action
};
```

### 4.2 Input Packet Structure

```cpp
struct InputPacket {
    uint32_t sequenceNumber;  // For reconciliation
    uint32_t serverTick;      // Last received server tick
    uint8_t  inputFlags;      // Bitfield: forward, back, left, right, jump, interact
    float    yaw;             // Camera yaw (for movement direction)
    float    pitch;           // Camera pitch
};
```

**Input Flags:**
```cpp
enum InputFlags : uint8_t {
    INPUT_FORWARD   = 1 << 0,
    INPUT_BACKWARD  = 1 << 1,
    INPUT_LEFT      = 1 << 2,
    INPUT_RIGHT     = 1 << 3,
    INPUT_JUMP      = 1 << 4,
    INPUT_INTERACT  = 1 << 5,
    INPUT_SPRINT    = 1 << 6,
};
```

### 4.3 State Delta Packet

```cpp
struct PlayerState {
    uint32_t playerId;
    glm::vec3 position;
    float yaw;
    float pitch;
    uint8_t animationState;  // Idle, walking, running, jumping
};

struct StateDeltaPacket {
    uint32_t serverTick;
    uint8_t playerCount;
    PlayerState players[];  // Variable length
};
```

### 4.4 Channel Assignment (ENet)

| Channel | Reliability | Use Case |
|---------|-------------|----------|
| 0 | Unreliable | Position updates (high frequency, OK to drop) |
| 1 | Reliable | Join/leave, entity spawn/destroy, chat |
| 2 | Reliable Sequenced | Important game events (interactions, damage) |

---

## 5. Entity Synchronization

### 5.1 Network Entity System

Create a new `NetworkEntity` class that extends or wraps `Entity`:

```cpp
// include/eden/NetworkEntity.hpp
class NetworkEntity : public Entity {
public:
    NetworkEntity(uint32_t networkId, uint32_t ownerId);
    
    uint32_t getNetworkId() const { return m_networkId; }
    uint32_t getOwnerId() const { return m_ownerId; }  // 0 = server-owned
    
    bool isLocallyControlled() const;  // True if this client owns it
    bool isAI() const { return m_isAI; }
    
    // Interpolation support
    void pushState(const glm::vec3& pos, const glm::quat& rot, float timestamp);
    void interpolate(float renderTime);
    
private:
    uint32_t m_networkId;
    uint32_t m_ownerId;
    bool m_isAI = false;
    
    // State buffer for interpolation (ring buffer of recent states)
    struct StateSnapshot {
        glm::vec3 position;
        glm::quat rotation;
        float timestamp;
    };
    std::array<StateSnapshot, 32> m_stateBuffer;
    int m_stateBufferHead = 0;
};
```

### 5.2 Player Representation

Each connected player (human or AI) gets a `NetworkEntity`:

```cpp
struct Player {
    uint32_t networkId;
    uint32_t entityId;          // Reference to NetworkEntity
    std::string name;
    bool isAI;
    ENetPeer* peer;             // nullptr for AI agents
    WebSocketConnection* wsConn; // nullptr for human players
    
    // Server-side state
    InputPacket lastInput;
    glm::vec3 serverPosition;
    float serverYaw, serverPitch;
};
```

---

## 6. AI Gateway Integration

### 6.1 WebSocket Protocol (JSON-based for AI simplicity)

**Perception Message (Server → AI):**
```json
{
    "type": "perception",
    "tick": 12345,
    "self": {
        "id": 5,
        "position": [100.5, 10.0, 200.3],
        "heading": 45.0,
        "health": 100
    },
    "nearbyEntities": [
        {"id": 2, "type": "player", "name": "Doc", "position": [95.0, 10.0, 198.0], "distance": 6.2},
        {"id": 8, "type": "npc", "name": "Trader Bob", "position": [110.0, 10.0, 205.0], "distance": 11.5}
    ],
    "terrain": {
        "biome": "plains",
        "heightAtFeet": 10.0,
        "nearbyFeatures": ["river to north", "forest to east"]
    }
}
```

**Action Message (AI → Server):**
```json
{
    "type": "action",
    "action": "move",
    "params": {
        "direction": "north",
        "speed": "walk"
    }
}
```

**Supported Actions:**
- `move` — direction (north/south/east/west/toward:<entityId>) + speed (walk/run/stop)
- `look` — yaw/pitch adjustment or "at:<entityId>"
- `interact` — target entity ID
- `say` — message (becomes world chat from AI)
- `emote` — animation trigger

### 6.2 AI Gateway Server

```cpp
// src/Network/AIGateway.hpp
class AIGateway {
public:
    AIGateway(uint16_t port = 7778);
    
    void start();
    void stop();
    void update();  // Call each server tick
    
    // Send perception to specific AI agent
    void sendPerception(uint32_t aiPlayerId, const PerceptionData& data);
    
    // Callback when AI sends action
    using ActionCallback = std::function<void(uint32_t aiPlayerId, const AIAction& action)>;
    void setActionCallback(ActionCallback cb);
    
private:
    // WebSocket server (use Beast or another WS library)
    std::unique_ptr<WebSocketServer> m_wsServer;
    std::unordered_map<uint32_t, WebSocketConnection*> m_aiConnections;
};
```

---

## 7. Implementation Phases

### Phase 1: Basic Networking (Week 1-2)

**Goal:** Two EDEN instances can see each other move.

**Tasks:**
1. Add ENet to CMakeLists.txt
2. Create `NetworkManager` class with client/server modes
3. Implement join/leave protocol
4. Send/receive position updates (unreliable channel)
5. Render other players as simple capsules/cubes

**Files to Create:**
```
src/Network/
├── NetworkManager.hpp
├── NetworkManager.cpp
├── NetworkProtocol.hpp      # Packet definitions
├── NetworkEntity.hpp
├── NetworkEntity.cpp
└── Serialization.hpp        # Serialize/deserialize helpers
```

**Test:** Run two instances locally, see both players move in real-time.

---

### Phase 2: Server Authority (Week 3)

**Goal:** Server validates all movement; clients predict + reconcile.

**Tasks:**
1. Move physics simulation to server
2. Implement client-side prediction
3. Add server reconciliation (rewind/replay on mismatch)
4. Handle latency compensation

**Key Code:**
```cpp
// Client-side prediction
void NetworkManager::processLocalInput(const InputPacket& input) {
    // 1. Apply input locally (prediction)
    m_localPredictedState = simulateInput(m_localPredictedState, input);
    
    // 2. Send to server
    sendInput(input);
    
    // 3. Store for reconciliation
    m_pendingInputs.push_back({input.sequenceNumber, input, m_localPredictedState});
}

// Server reconciliation
void NetworkManager::onServerState(const PlayerState& serverState, uint32_t lastProcessedInput) {
    // Remove acknowledged inputs
    while (!m_pendingInputs.empty() && m_pendingInputs.front().seq <= lastProcessedInput) {
        m_pendingInputs.pop_front();
    }
    
    // Re-simulate remaining inputs from server state
    PlayerState reconciled = serverState;
    for (const auto& pending : m_pendingInputs) {
        reconciled = simulateInput(reconciled, pending.input);
    }
    
    m_localPredictedState = reconciled;
}
```

---

### Phase 3: AI Gateway (Week 4)

**Goal:** AI agents can connect and interact with the world.

**Tasks:**
1. Add WebSocket server (Beast or standalone library)
2. Implement perception serialization
3. Implement action parsing + validation
4. Rate limiting + sandboxing for AI actions
5. Test with OpenClaw agent

**Integration with OpenClaw:**

Create a skill or simple script that:
1. Connects to `ws://server:7778`
2. Receives perception updates
3. Decides actions based on world state
4. Sends action commands

---

### Phase 4: Polish & Scale (Week 5+)

**Goal:** Production-ready multiplayer.

**Tasks:**
1. Entity interpolation for smooth rendering
2. Interest management (only send nearby entities)
3. Terrain chunk synchronization (for terrain modifications)
4. Persistence (PostgreSQL or SQLite for world state)
5. Authentication (simple token-based initially)
6. NAT punch-through or relay fallback

---

## 8. File Structure Changes

```
EDEN/
├── CMakeLists.txt                    # Add enet, websocket deps
├── include/eden/
│   ├── NetworkEntity.hpp             # NEW
│   └── NetworkTypes.hpp              # NEW (shared enums/structs)
├── src/
│   └── Network/
│       ├── AsyncHttpClient.cpp       # Existing
│       ├── NetworkManager.hpp        # NEW
│       ├── NetworkManager.cpp        # NEW
│       ├── NetworkProtocol.hpp       # NEW
│       ├── Serialization.hpp         # NEW
│       ├── AIGateway.hpp             # NEW
│       └── AIGateway.cpp             # NEW
└── examples/
    ├── terrain_editor/               # Add networking integration
    │   └── main.cpp                  # Add NetworkManager
    └── dedicated_server/             # NEW - headless server
        ├── CMakeLists.txt
        └── main.cpp
```

---

## 9. CMakeLists.txt Modifications

Add after existing FetchContent declarations:

```cmake
# Fetch ENet (UDP networking)
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
)
FetchContent_MakeAvailable(enet)

# Note: For AI Gateway WebSocket, can use Beast (already have Boost?)
# or fetch a standalone WebSocket library like:
# FetchContent_Declare(
#     websocketpp
#     GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
#     GIT_TAG 0.8.2
# )
```

Add to `EDEN_SOURCES`:
```cmake
set(EDEN_SOURCES
    ${EDEN_SOURCES}
    src/Network/NetworkManager.cpp
    src/Network/AIGateway.cpp
)
```

Link ENet:
```cmake
target_link_libraries(terrain_editor PRIVATE enet)
target_link_libraries(dedicated_server PRIVATE enet)
```

---

## 10. Key Injection Points

### 10.1 VulkanApplicationBase::run() → Main Loop Hook

```cpp
// In derived class (TerrainEditor or new MultiplayerGame)
void update(float deltaTime) override {
    // NEW: Process network before game logic
    if (m_networkManager) {
        m_networkManager->update(deltaTime);
    }
    
    // Existing game logic
    handleCameraInput(deltaTime);
    m_terrain.update(m_camera.getPosition());
    
    // NEW: Send local player state to server (if client)
    if (m_networkManager && m_networkManager->isClient()) {
        m_networkManager->sendLocalPlayerState(m_camera.getPosition(), m_camera.getYaw(), m_camera.getPitch());
    }
}
```

### 10.2 Camera → Network Player Sync

```cpp
// In update loop, after receiving network state
void TerrainEditor::updateNetworkPlayers(float deltaTime) {
    for (auto& [id, player] : m_networkManager->getRemotePlayers()) {
        // Interpolate position for smooth rendering
        player.entity->interpolate(m_renderTime);
        
        // Update visual representation
        auto* sceneObj = getSceneObjectForPlayer(id);
        if (sceneObj) {
            sceneObj->getTransform().setPosition(player.entity->getInterpolatedPosition());
        }
    }
}
```

### 10.3 Dedicated Server Entry Point

```cpp
// examples/dedicated_server/main.cpp
#include "Network/NetworkManager.hpp"
#include "Network/AIGateway.hpp"
#include <eden/Terrain.hpp>
#include <eden/JoltCharacter.hpp>

int main(int argc, char** argv) {
    // No Vulkan, no window — headless
    
    TerrainConfig config;
    config.useFixedBounds = true;
    config.wrapWorld = true;
    Terrain terrain(config);
    terrain.preloadAllChunks();
    
    NetworkManager network;
    network.startServer(7777);
    
    AIGateway aiGateway(7778);
    aiGateway.start();
    
    // Fixed timestep server loop
    const float TICK_RATE = 60.0f;
    const float TICK_DURATION = 1.0f / TICK_RATE;
    
    while (running) {
        auto tickStart = std::chrono::high_resolution_clock::now();
        
        // Process incoming packets
        network.update(TICK_DURATION);
        aiGateway.update();
        
        // Simulate physics for all players
        for (auto& [id, player] : network.getPlayers()) {
            glm::vec3 newPos = player.physics->update(
                TICK_DURATION,
                inputToVelocity(player.lastInput),
                player.lastInput.inputFlags & INPUT_JUMP
            );
            player.serverPosition = terrain.wrapWorldPosition(newPos);
        }
        
        // Broadcast state to all clients
        network.broadcastState();
        
        // Sleep to maintain tick rate
        auto tickEnd = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<float>(tickEnd - tickStart).count();
        if (elapsed < TICK_DURATION) {
            std::this_thread::sleep_for(
                std::chrono::duration<float>(TICK_DURATION - elapsed)
            );
        }
    }
    
    return 0;
}
```

---

## 11. Testing Strategy

### 11.1 Local Testing

1. **Loopback server:** Run server + client on same machine
2. **LAN testing:** Two machines on same network
3. **Simulated latency:** Use `tc` (Linux traffic control) to add artificial delay

### 11.2 AI Agent Testing

1. **Echo bot:** Simple agent that mirrors player movement
2. **Wanderer:** Random exploration agent
3. **OpenClaw integration:** Connect Xenk as world observer

---

## 12. Security Considerations

1. **Server authority:** Never trust client position directly
2. **Rate limiting:** Cap input packets (max 60/sec) and AI actions (max 2/sec)
3. **Validation:** Check movement deltas for impossible speeds
4. **Authentication:** Token-based join (prevent impersonation)
5. **AI sandboxing:** AI agents have limited action set, no direct world modification

---

## 13. Next Steps

1. **Approve architecture** — Any changes to the above?
2. **Create NetworkManager skeleton** — I can write initial header/implementation
3. **Add ENet to CMake** — Verify it builds
4. **Implement Phase 1** — Basic client/server join + position sync
5. **Test locally** — Two instances seeing each other

---

## Appendix A: Reference Implementations

- **Quake 3 Networking:** Classic client-side prediction model
- **Source Engine:** Lag compensation, interpolation
- **Overwatch GDC Talk:** "Overwatch Gameplay Architecture and Netcode"
- **Gabriel Gambetta Articles:** https://www.gabrielgambetta.com/client-server-game-architecture.html

---

*Document prepared for Claude Code implementation. Each phase is scoped for ~1-2 week sprints.*
