# AI Companion Module for EDEN

Pluggable AI conversation system that enables NPCs and AI agents to engage in natural dialogue with players.

## Features

- **Being Types**: Support for various entity types (Human, Robot, Android, Alien, AI Architect, etc.)
- **Conversation Management**: Async HTTP communication with AI backend
- **Multiple Providers**: Grok, Ollama, and extensible for others
- **ImGui UI**: Built-in conversation interface
- **Modular Design**: Easy to integrate into any EDEN application

## Directory Structure

```
ai_companion/
├── include/
│   ├── AICompanionModule.hpp    # Main module interface
│   ├── BeingTypes.hpp           # Being type definitions
│   └── ConversationManager.hpp  # Conversation session management
├── src/
│   ├── AICompanionModule.cpp
│   └── ConversationManager.cpp
├── backend/
│   └── server.py                # AI backend server (FastAPI)
├── CMakeLists.txt
└── README.md
```

## Usage

### 1. Add to your CMakeLists.txt

```cmake
add_subdirectory(modules/ai_companion)
target_link_libraries(your_app PRIVATE eden::ai_companion)
```

### 2. Implement IConversable interface

```cpp
#include <ai_companion/AICompanionModule.hpp>

class MyNPC : public eden::ai::IConversable {
public:
    std::string getName() const override { return m_name; }
    eden::ai::BeingType getBeingType() const override { return m_beingType; }
    glm::vec3 getPosition() const override { return m_position; }
    
private:
    std::string m_name;
    eden::ai::BeingType m_beingType;
    glm::vec3 m_position;
};
```

### 3. Initialize the module

```cpp
#include <ai_companion/AICompanionModule.hpp>

class MyGame : public VulkanApplicationBase {
    std::unique_ptr<eden::ai::AICompanionModule> m_ai;
    
    void onInit() override {
        eden::ai::AICompanionConfig config;
        config.backendUrl = "http://localhost:8080";
        config.interactionRange = 3.0f;
        
        m_ai = std::make_unique<eden::ai::AICompanionModule>();
        m_ai->initialize(config);
    }
    
    void update(float dt) override {
        m_ai->update(dt);
        
        // Handle interaction (E key)
        if (playerPressedInteract) {
            auto* nearest = findNearestNPC();
            if (m_ai->canInteract(nearest, playerPos)) {
                m_ai->startConversation(nearest);
            }
        }
        
        // Handle escape to end conversation
        if (escapePressed && m_ai->isInConversation()) {
            m_ai->endConversation();
        }
    }
    
    void renderUI() override {
        m_ai->renderConversationUI();
    }
};
```

## Being Types

| Type | ID | Description |
|------|----|-------------|
| STATIC | 0 | Non-interactive object |
| HUMAN | 1 | Human character |
| CLONE | 2 | Cloned human |
| ROBOT | 3 | Mechanical robot |
| ANDROID | 4 | Human-like robot |
| CYBORG | 5 | Human-machine hybrid |
| ALIEN | 6 | Extraterrestrial |
| EVE | 7 | Eve companion (special) |
| AI_ARCHITECT | 8 | Xenk / AI world architect |

## Backend Server

The backend server (`backend/server.py`) provides the AI inference.

### Requirements

```bash
pip install fastapi uvicorn httpx python-dotenv pydantic
```

### Configuration

Create `.env` file:

```
XAI_API_KEY=your_grok_api_key
GROK_MODEL=grok-3
OLLAMA_URL=http://localhost:11434
OLLAMA_MODEL=llama3
DEFAULT_PROVIDER=grok
```

### Run

```bash
cd backend
python server.py
```

Server runs on `http://localhost:8080` by default.

## API Endpoints

- `GET /health` - Health check
- `POST /chat` - Send message, get response
- `POST /session` - Create new session
- `GET /providers` - List available providers

## License

MIT - Same as EDEN engine.
