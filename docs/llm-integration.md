# Setting Up an AI Provider

Slag Legion's AI Architects (AIAs) need a language model to think and speak. You can play the game without an AIA, but it's designed to be played with one or more. You can use a paid cloud API or run a free model locally on your own hardware.

Each API key you add gives you 1 AIA, so you could have a Grok AIA, a Claude AIA, and so on. You can also create up to 10 AIAs per provider, each with their own memories, personality, and specializations — not sure why anyone would need that many, but use cases may emerge.

To use a paid LLM you need an API key. Below is a basic guide to getting keys from each provider, though steps may change as providers update their websites. **NEVER share your API key with anyone — keep it stored in a secure place.**

To use a free model from Ollama you don't need an API key, but please note that many of the smaller free models will not work as well as the frontier models. However, we have achieved some amazing results with small models, and it is theoretically possible for very determined players to create comparable results depending on the use case and how individual models handle Grove scripting. It's also worth noting that free models run from your local computer, so inference will be slow unless you have a very powerful system.

## Paid Providers

### Grok (xAI)

1. Go to [console.x.ai](https://console.x.ai/)
2. Sign up or log in
3. Navigate to **API Keys**
4. Click **Create API Key**
5. Copy the key — it starts with `xai-`

Recommended model: `grok-3`

### OpenAI (ChatGPT)

1. Go to [platform.openai.com](https://platform.openai.com/)
2. Sign up or log in
3. Go to **API Keys** in the left sidebar
4. Click **Create new secret key**
5. Copy the key — it starts with `sk-`

Recommended model: `gpt-4o`

### Anthropic (Claude)

1. Go to [console.anthropic.com](https://console.anthropic.com/)
2. Sign up or log in
3. Go to **API Keys**
4. Click **Create Key**
5. Copy the key — it starts with `sk-ant-`

Recommended model: `claude-sonnet-4-5-20250929`

### Google (Gemini)

1. Go to [aistudio.google.com](https://aistudio.google.com/)
2. Sign in with your Google account
3. Click **Get API Key** in the top left
4. Click **Create API Key**
5. Copy the key

Recommended model: `gemini-2.0-flash`

## Free Local Models (Ollama)

Ollama lets you run language models entirely on your own machine — no API key, no cost, no internet required.

### Install Ollama

**Linux:**
```bash
curl -fsSL https://ollama.ai/install.sh | sh
```

**macOS / Windows:** Download from [ollama.ai](https://ollama.ai)

### Download a Model

```bash
ollama pull mistral
```

That's it. Ollama runs in the background automatically.

### Recommended Models

| Model | Download Size | VRAM Needed | Notes |
|-------|--------------|-------------|-------|
| `mistral` | ~4 GB | 8 GB | Good balance of speed and quality |
| `llama3` | ~4 GB | 8 GB | Meta's latest, strong general purpose |
| `dolphin-mixtral:8x7b` | ~26 GB | 32 GB+ | Best quality, needs powerful GPU |
| `neural-chat` | ~4 GB | 8 GB | Good conversational ability |
| `phi3` | ~2 GB | 4 GB | Lightweight, runs on most hardware |

### Verify It's Running

```bash
ollama list
```

You should see your downloaded model. Ollama serves on `http://localhost:11434` by default.

## Which Should I Choose?

| | Paid API | Ollama |
|--|----------|--------|
| **Cost** | Pay per use (~$0.01-0.10 per conversation) | Free |
| **Quality** | Frontier models, best reasoning | Good, depends on model size |
| **Speed** | Fast (cloud servers) | Depends on your GPU |
| **Privacy** | Messages sent to cloud | Everything stays on your machine |
| **Internet** | Required | Not required |
| **Setup** | Get API key, paste it in | Install Ollama, pull a model |

For the best NPC behavior (especially for construction and complex commands), a frontier model like Grok or Claude is recommended. For casual conversation and experimentation, Ollama with `mistral` works well.
