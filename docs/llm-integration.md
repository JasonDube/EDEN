# Integrating LLMs into EDEN

This guide walks you through adding AI-powered NPCs to your EDEN world. By the end, you'll have an NPC that responds to natural language, moves around, builds structures, and executes Grove scripts.

## What You Need

Pick one:

- **Paid API** — A key from [xAI (Grok)](https://console.x.ai/), OpenAI, Anthropic, or Google
- **Free local model** — [Ollama](https://ollama.ai) running on your machine

## Step 1: Start the Backend

The backend is a Python server that bridges your NPCs to the LLM.

```bash
cd backend
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

Copy the example config and add your credentials:

```bash
cp .env.example .env
```

Edit `.env` with your provider:

=== "Grok (xAI)"

    ```bash
    XAI_API_KEY=your-key-from-console.x.ai
    GROK_MODEL=grok-3
    DEFAULT_PROVIDER=grok
    ```

=== "Ollama (Free, Local)"

    ```bash
    OLLAMA_URL=http://localhost:11434
    OLLAMA_MODEL=dolphin-mixtral:8x7b
    DEFAULT_PROVIDER=ollama
    ```

    Install Ollama first, then pull a model:
    ```bash
    ollama pull dolphin-mixtral:8x7b
    ollama serve
    ```

Start the server:

```bash
python server.py
```

You should see: `Starting server on http://localhost:8080`

Verify it's working:

```bash
curl http://localhost:8080/health
```

## Step 2: Create an NPC in the Editor

1. Open the **Terrain Editor** and load or create a level
2. Spawn an object — a cube, cylinder, or load a `.glb` model
3. **Name it** something memorable (e.g., "Xenk", "Guard_1", "Merchant")
4. In the **Properties panel**, set the **Being Type**

### Being Types

| Type | ID | Personality | Best For |
|------|-----|------------|----------|
| Human | 1 | Natural, conversational | Merchants, villagers |
| Clone | 2 | Existential, identity-questioning | Story NPCs |
| Robot | 3 | Mechanical, efficient | Workers, guards |
| Android | 4 | Polite, human-like | Assistants |
| Cyborg | 5 | Aggressive, military | Combat NPCs |
| Alien | 6 | Non-human, unusual speech | Exotic encounters |
| Eve | 7 | Companion android | Ship companion |
| AI Architect | 8 | Logical, builder-focused | Construction, economy, bots |
| AlgoBot | 9 | No chat — script-driven | Programmable workers |

The **AI Architect** (type 8) is the most capable — it can build structures, program AlgoBots, manage the economy, and execute Grove scripts. Start with this type if you're experimenting.

## Step 3: Talk to Your NPC

1. **Save your level** (File > Save)
2. Press **P** to enter Play Mode
3. Walk up to your NPC (within ~15 meters)
4. Press **E** to interact
5. Type a message and press Enter

The NPC sees what's around it through a **perception cone** — a 120-degree, 50-meter scan of nearby objects. It knows the names, positions, and types of everything in view, so you can say things like "go to that cube" or "pick up the timber."

## What NPCs Can Do

Every sentient NPC can:

- **Look around** — scan the environment
- **Move to** locations — walk to a world position
- **Turn to** face objects or directions
- **Follow** the player continuously
- **Stop** any current action

The **AI Architect** (type 8) can additionally:

- **Build structures** — spawn cubes, cylinders, models via Grove
- **Program AlgoBots** — upload behavior scripts to worker bots
- **Manage economy** — buy/sell land, check credits
- **Run scripts** — execute any Grove code on demand

## How It Works Under the Hood

When you press E and type a message:

1. The editor captures a **perception snapshot** of what the NPC can see
2. Your message + perception data is sent to the backend via HTTP
3. The backend forwards it to the LLM with the NPC's personality prompt
4. The LLM responds with dialogue and optional **action commands**
5. The editor displays the dialogue and executes any actions

```
Player types "build a house"
        ↓
   [HTTP POST /chat]
        ↓
   Backend (server.py)
        ↓
   LLM (Grok/Ollama/Claude)
        ↓
   {"response": "Initiating construction.", "action": {"type": "run_script", "script": "..."}}
        ↓
   Editor executes Grove script → objects appear in world
```

The NPC's response can include motor commands like `move_to`, `pickup`, `run_script`, etc. The editor parses the JSON action and executes it in the game world.

## Configuring Providers

### Switching Providers at Runtime

The backend supports multiple providers simultaneously. You can override the default per-request by specifying a provider in the API call. The editor uses whatever `DEFAULT_PROVIDER` is set in `.env`.

### Provider Comparison

| Provider | Cost | Speed | Quality | Offline |
|----------|------|-------|---------|---------|
| Grok (xAI) | Paid API | Fast | High | No |
| Ollama | Free | Varies by model/hardware | Good | Yes |

### Ollama Model Recommendations

| Model | Size | Notes |
|-------|------|-------|
| `dolphin-mixtral:8x7b` | ~26 GB | Best quality, needs good GPU |
| `mistral` | ~4 GB | Good balance of speed and quality |
| `llama2` | ~4 GB | Solid general purpose |
| `neural-chat` | ~4 GB | Good at conversational dialogue |

## Persistent Memory

The **AI Architect** and **Eve** NPCs maintain persistent context across sessions:

- `backend/xenk_context.txt` — The architect's accumulated world knowledge
- `backend/xenk_briefing.txt` — Current session briefing
- `backend/logs/chat_history.jsonl` — Full conversation logs

This means your AI architect remembers what it built, what land was purchased, and what instructions you gave — even after restarting the game.

## Troubleshooting

**NPC says "(AI unavailable)"**

- Backend server isn't running — start it with `python server.py`
- Wrong URL — the editor connects to `http://localhost:8080` by default

**NPC doesn't respond**

- Check the terminal where `server.py` is running for error messages
- Verify your API key is correct in `.env`
- Run `curl http://localhost:8080/health` to check provider status

**Ollama is slow**

- Larger models need more VRAM — try a smaller model like `mistral`
- Check GPU utilization with `nvidia-smi`

**NPC doesn't move when asked**

- Only being types with motor control can move (Robot, Eve, AI Architect)
- Make sure you're in Play Mode (press P)
- The NPC needs perception data to know where objects are — it can only reference things it can "see"
