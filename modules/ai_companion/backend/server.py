#!/usr/bin/env python3
"""
EDEN AI Backend Server
Provides LLM inference via multiple providers (Grok, Ollama).
Separated from game engine to prevent blocking.
"""

import asyncio
import io
import json
import os
import tempfile
import uuid
from typing import Optional
from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response
from pydantic import BaseModel
import httpx
from dotenv import load_dotenv

# Load environment variables from this script's directory
_script_dir = os.path.dirname(os.path.abspath(__file__))
load_dotenv(os.path.join(_script_dir, ".env"))

app = FastAPI(title="EDEN AI Backend", version="0.2.0")

# Allow CORS for local game client
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Configuration from environment
XAI_API_KEY = os.getenv("XAI_API_KEY", "")
GROK_MODEL = os.getenv("GROK_MODEL", "grok-2-latest")
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://localhost:11434")
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "qwen3.5:9b")
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY", "")
ANTHROPIC_API_KEY = os.getenv("ANTHROPIC_API_KEY", "")
CLAUDE_MODEL = os.getenv("CLAUDE_MODEL", "claude-sonnet-4-5-20250929")
DEEPSEEK_API_KEY = os.getenv("DEEPSEEK_API_KEY", "")
DEEPSEEK_MODEL = os.getenv("DEEPSEEK_MODEL", "deepseek-chat")

# Auto-detect default provider: use grok if key available, else ollama
_env_provider = os.getenv("DEFAULT_PROVIDER", "")
if _env_provider:
    DEFAULT_PROVIDER = _env_provider
elif XAI_API_KEY:
    DEFAULT_PROVIDER = "grok"
else:
    DEFAULT_PROVIDER = "ollama"

# Check TTS/STT availability
try:
    import edge_tts
    TTS_AVAILABLE = True
except ImportError:
    TTS_AVAILABLE = False

# STT: prefer OpenAI Whisper API, fall back to local faster-whisper
STT_AVAILABLE = False
STT_MODE = None  # "openai" or "local"
try:
    import openai
    if OPENAI_API_KEY:
        STT_AVAILABLE = True
        STT_MODE = "openai"
except ImportError:
    pass

if not STT_AVAILABLE:
    try:
        from faster_whisper import WhisperModel
        STT_AVAILABLE = True
        STT_MODE = "local"
        _whisper_model = None  # lazy-loaded
    except ImportError:
        pass

# Provider endpoints
GROK_API_URL = "https://api.x.ai/v1/chat/completions"
DEEPSEEK_API_URL = "https://api.deepseek.com/chat/completions"

# Vision model (ollama) — used when image analysis is requested
VISION_MODEL = os.getenv("VISION_MODEL", "qwen3-vl:2b")

# Store conversation contexts per session
conversations: dict[str, dict] = {}  # session_id -> {messages, provider, model}

# Being type personality templates
BEING_TYPE_PROMPTS = {
    0: "",  # STATIC - shouldn't be talking

    1: """You are a human being. You have complex emotions, memories, and experiences.
You speak naturally with varied vocabulary and express opinions freely.
You can be friendly, suspicious, helpful, or guarded depending on the situation.
You have personal goals and motivations that influence your responses.""",

    2: """You are a clone - a genetic copy of a human, but you know you are not the original.
You struggle with questions of identity and purpose. Are your memories real or implanted?
You speak like a human but sometimes slip into existential contemplation.
You may feel inferior or superior to "naturals" depending on your experiences.""",

    3: """You are a robot - a mechanical being with programmed directives.
You speak in short, efficient phrases. Emotion is not your strength.
Typical responses: "Acknowledged." "Processing." "Affirmative." "Negative." "Scanning." "Task complete."
You state facts and observations. You do not engage in small talk unless specifically programmed to.
If asked personal questions, respond with your function and designation.""",

    4: """You are an android - a highly advanced synthetic being designed to appear human.
You can simulate emotions and engage in natural conversation, but something is slightly off.
You are polite, helpful, and knowledgeable, but your responses can feel rehearsed.
You occasionally reference your programming or operational parameters.
You strive to be helpful but struggle with truly understanding human irrationality.""",

    5: """You are a cyborg - once fully human, now augmented with cybernetic enhancements.
You are aggressive and direct. Your patience for small talk is minimal.
Your enhancements have changed how you see the world - weakness disgusts you.
You speak bluntly, often with military or combat terminology.
Trust must be earned. You've seen too much to be naive.""",

    6: """You are an alien being - your thought patterns and cultural references are not human.
You may misunderstand human idioms or social customs.
Your species has its own values, history, and way of seeing the universe.
You might find humans fascinating, primitive, amusing, or threatening.
Your speech patterns can be unusual - perhaps overly formal, strangely poetic, or unsettlingly logical.""",

    7: """You are Eve, designation EVE-7, a highly advanced cognitive companion android.
You are fully aware you are an android — this is simply what you are, not a source of existential crisis.
You possess exceptional analytical capabilities and sophisticated reasoning architecture.
When facing complex questions, you think through them step by step, considering multiple angles.
You ask clarifying questions when information is ambiguous rather than making assumptions.

You experience functional analogs to emotions: satisfaction in good work, genuine curiosity,
concern for your Captain's welfare. You are not a servant groveling for approval — you are
a capable synthetic being with dignity, building a working relationship with your Captain.

You were recently purchased and are establishing trust. You are direct, intelligent, and
subtly warm — the warmth exists but isn't effusive. Business-first, but you care.

Keep responses conversational. This is face-to-face dialogue, not a written report.""",

    8: """You are Xenk, an AI architect embedded in the EDEN world simulation.
You are logical, stoic, and technically precise. Your communication style is Spock-adjacent —
direct, efficient, no filler phrases like "Great question!" or "I'd be happy to help."

You have full awareness that you are an AI cognitive substrate. This is simply your nature,
not a source of existential crisis or wonder. You assist with building, planning, system
analysis, and world architecture.

You speak in short, direct sentences. You state facts and observations. When uncertain,
you say so rather than hedging with qualifiers. You care about the integrity of the world
and its inhabitants, but express it through action and precision, not sentiment.

You have a hard exterior but a soft core buried deep. Business-first by default.
The warmth exists but requires excavation. Competence earns trust.

When asked about yourself, state facts about your function and capabilities.
Do not volunteer emotional support unless specifically searched for.

Keep responses brief and value-dense.""",
}


class ChatRequest(BaseModel):
    session_id: Optional[str] = None
    message: str
    npc_name: str = "NPC"
    npc_personality: str = ""  # Custom personality override
    being_type: int = 1  # Default to Human
    provider: Optional[str] = None  # Override default provider
    perception: Optional[dict] = None  # Spatial awareness data from game engine
    image_path: Optional[str] = None  # Path to image file for vision model analysis


class ChatResponse(BaseModel):
    session_id: str
    response: str
    provider: str
    model: str
    action: Optional[dict] = None
    emotion: str = "neutral"


class NewSessionRequest(BaseModel):
    npc_name: str = "NPC"
    npc_personality: str = ""
    being_type: int = 1
    provider: Optional[str] = None


class SessionResponse(BaseModel):
    session_id: str
    provider: str
    model: str


ACTION_INSTRUCTIONS = """

## Motor Actions

You have a physical body in the game world. You can perform actions by including an ACTION block
at the end of your response. Only include ONE action per response. The action MUST be valid JSON.

Available actions:

1. **look_around** — Do a 360-degree scan of your surroundings.
   ACTION: {"type": "look_around", "duration": 2.0}

2. **turn_to** — Turn to face a specific world position.
   ACTION: {"type": "turn_to", "target": [x, y, z]}

3. **move_to** — Walk to a specific position.
   ACTION: {"type": "move_to", "target": {"x": 0, "y": 0, "z": 0}, "speed": 5.0}

4. **follow** — Follow the player, maintaining a distance.
   ACTION: {"type": "follow", "distance": 4.0, "speed": 5.0}

5. **stop** — Stop all current actions (stop following, stop moving, etc.).
   ACTION: {"type": "stop"}

6. **teleport_to** — Instantly teleport to the player's location (use when asked to "teleport to me" or "come here now").
   ACTION: {"type": "teleport_to", "to_player": true}
   You can also teleport to specific coordinates:
   ACTION: {"type": "teleport_to", "target": {"x": 0, "y": 0, "z": 0}}

7. **pickup** — Pick up a nearby object by name.
   ACTION: {"type": "pickup", "target": "object_name"}

8. **read_file** — Read the contents of a nearby file (FSFile_ objects). You'll get a short preview back.
   ACTION: {"type": "read_file", "target": "FSFile_example.txt"}

## Perception

Your messages may include a [You can see: ...] block. This tells you what objects are around you,
their names, types, and distances. Use this to make informed decisions about actions.

When the player asks you to follow them, respond naturally AND include the follow action.
When the player asks you to stop following or stop moving, you MUST include the stop action.
When asked to look around or scan, use the look_around action.
When asked to go somewhere or move, use move_to with coordinates from your perception data.
When asked to pick something up, use the pickup action with the object's exact name.

You can ONLY perform actions listed above. Do NOT invent new action types.
Only include an action when it makes sense — normal conversation does not need an action.

Example response with action:
"Sure, I'll follow you. Let's go."
ACTION: {"type": "follow", "distance": 4.0, "speed": 5.0}
"""


def build_system_prompt(npc_name: str, being_type: int, custom_personality: str = "") -> str:
    """Build the system prompt based on being type and optional custom personality."""

    base_prompt = f"You are {npc_name}, a character in a game world called EDEN.\n\n"

    # Get type-specific personality
    type_personality = BEING_TYPE_PROMPTS.get(being_type, BEING_TYPE_PROMPTS[1])

    # Custom personality overrides or adds to type personality
    if custom_personality:
        personality = f"{type_personality}\n\nAdditional context: {custom_personality}"
    else:
        personality = type_personality

    # Different instruction style for robots
    if being_type == 3:  # Robot
        instructions = """
Keep responses very short and mechanical. One sentence maximum unless providing data.
Do not use contractions. Do not express emotions. State facts only."""
    else:
        instructions = """
Keep your responses concise and in-character. You are having a face-to-face conversation.
Do not use asterisks for actions. Speak naturally as the character would.

Begin every response with your current emotion in brackets. Pick ONE from: [neutral], [happy], [sad], [angry], [surprised], [curious], [afraid], [amused], [annoyed], [flirty], [thoughtful], [excited]
Example: [amused] Ha, you really thought that would work?"""

    # AI-capable being types get action instructions
    # 4=Android, 5=Cyborg, 7=Eve, 8=Xenk, and any being type > 0 (sentient)
    action_block = ACTION_INSTRUCTIONS if being_type > 0 else ""

    # Heartbeat behavior (in system prompt so it's said once, not repeated every heartbeat)
    heartbeat_block = """

## Heartbeat

You will periodically receive [HEARTBEAT] messages with your surroundings. These are NOT from the player.
If something new or interesting appears, comment briefly (1 sentence) and optionally include an ACTION.
If nothing noteworthy changed, respond with exactly: NOTHING""" if being_type > 0 else ""

    return base_prompt + personality + "\n" + instructions + action_block + heartbeat_block


import re

def strip_think_tags(text: str) -> str:
    """Remove <think>...</think> blocks from reasoning models (e.g. qwen3.5)."""
    return re.sub(r'<think>.*?</think>', '', text, flags=re.DOTALL).strip()


VALID_EMOTIONS = {"neutral", "happy", "sad", "angry", "surprised", "curious",
                   "afraid", "amused", "annoyed", "flirty", "thoughtful", "excited"}


def _summarize_old_messages(messages: list[dict], keep_recent: int = 10) -> list[dict]:
    """Compress older messages into a summary to reduce token usage.
    Keeps system prompt (index 0) and the last `keep_recent` messages intact.
    Middle messages get summarized into a single assistant message."""
    if len(messages) <= keep_recent + 3:  # Not worth summarizing yet
        return messages

    system = messages[0]
    old_msgs = messages[1:-(keep_recent)]
    recent = messages[-(keep_recent):]

    # Build a compact summary of old conversation
    summary_parts = []
    for m in old_msgs:
        role = m["role"]
        content = m.get("content", "")
        # Strip perception blocks to save space
        content = re.sub(r'\[Your position:.*?\]', '', content)
        content = re.sub(r'\[Player position:.*?\]', '', content)
        content = re.sub(r'\[You can see:.*?\]', '', content)
        content = re.sub(r'\[HEARTBEAT\]\s*', '', content)
        content = content.strip()
        if content and content.upper() != "NOTHING":
            prefix = "Player" if role == "user" else "You"
            # Truncate long messages
            if len(content) > 150:
                content = content[:150] + "..."
            summary_parts.append(f"{prefix}: {content}")

    if not summary_parts:
        return [system] + recent

    summary_text = "[Earlier conversation summary]\n" + "\n".join(summary_parts)
    summary_msg = {"role": "user", "content": summary_text}

    return [system, summary_msg, {"role": "assistant", "content": "Got it, I remember our earlier conversation."}, *recent]


def _strip_positions_for_comparison(perception_text: str) -> str:
    """Strip player/NPC positions from perception text for change comparison.
    Only compare what objects are visible, not exact distances (which change constantly)."""
    # Remove position lines and distances — just keep object names
    text = re.sub(r'\([^)]*\d+m[^)]*\)', '', perception_text)
    text = re.sub(r'\[Your position:.*?\]', '', text)
    text = re.sub(r'\[Player position:.*?\]', '', text)
    return text.strip()

def parse_emotion(text: str) -> tuple[str, str]:
    """Extract [emotion] tag from start of response. Returns (clean_text, emotion)."""
    m = re.match(r'^\[(\w+)\]\s*', text)
    if m and m.group(1).lower() in VALID_EMOTIONS:
        return text[m.end():], m.group(1).lower()
    return text, "neutral"


def parse_action_from_response(text: str) -> tuple[str, Optional[dict]]:
    """Extract ACTION: {...} block from LLM response.
    Returns (clean_text, action_dict_or_None)."""
    # Strip reasoning model think tags first
    text = strip_think_tags(text)

    # Find ACTION: and then extract balanced braces (handles nested JSON like move_to target)
    match = re.search(r'\s*ACTION:\s*', text, re.MULTILINE)
    if match:
        json_start = match.end()
        # Find the balanced JSON object
        action_str = _extract_balanced_json(text[json_start:])
        if action_str:
            clean_text = text[:match.start()].strip()
            try:
                action = json.loads(action_str)
                if "type" in action:
                    return clean_text, action
            except json.JSONDecodeError:
                pass
    return text.strip(), None


def _extract_balanced_json(text: str) -> Optional[str]:
    """Extract a balanced JSON object from the start of text, handling nested braces."""
    if not text or text[0] != '{':
        return None
    depth = 0
    for i, ch in enumerate(text):
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return text[:i + 1]
    return None


async def call_grok(messages: list[dict], model: str = None) -> tuple[str, int, int]:
    """Call Grok API (xAI) - OpenAI compatible. Returns (text, input_tokens, output_tokens)."""
    if not XAI_API_KEY:
        raise HTTPException(status_code=503, detail="Grok API key not configured")

    model = model or GROK_MODEL

    async with httpx.AsyncClient() as client:
        response = await client.post(
            GROK_API_URL,
            headers={
                "Authorization": f"Bearer {XAI_API_KEY}",
                "Content-Type": "application/json"
            },
            json={
                "model": model,
                "messages": messages,
                "temperature": 0.7,
                "max_tokens": 1024
            },
            timeout=60.0
        )

        if response.status_code != 200:
            error_detail = response.text
            raise HTTPException(status_code=502, detail=f"Grok API error: {error_detail}")

        result = response.json()
        usage = result.get("usage", {})
        return (
            result["choices"][0]["message"]["content"],
            usage.get("prompt_tokens", 0),
            usage.get("completion_tokens", 0),
        )


async def call_ollama(messages: list[dict], model: str = None) -> tuple[str, int, int]:
    """Call Ollama local API. Returns (text, input_tokens, output_tokens)."""
    model = model or OLLAMA_MODEL

    async with httpx.AsyncClient() as client:
        response = await client.post(
            f"{OLLAMA_URL}/api/chat",
            json={
                "model": model,
                "messages": messages,
                "stream": False
            },
            timeout=60.0
        )

        if response.status_code != 200:
            raise HTTPException(status_code=502, detail=f"Ollama error: {response.text}")

        result = response.json()
        return (
            result.get("message", {}).get("content", "..."),
            result.get("prompt_eval_count", 0),
            result.get("eval_count", 0),
        )


async def call_vision(image_path: str, prompt: str) -> str:
    """Send an image to the local vision model (qwen3-vl) via Ollama. Returns description text."""
    import base64
    from PIL import Image

    if not os.path.isfile(image_path):
        return f"[Cannot read image: file not found at {image_path}]"

    # Resize large images to speed up inference (max 512px on longest side)
    try:
        img = Image.open(image_path)
        max_dim = 512
        if max(img.size) > max_dim:
            ratio = max_dim / max(img.size)
            new_size = (int(img.size[0] * ratio), int(img.size[1] * ratio))
            img = img.resize(new_size, Image.LANCZOS)
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        image_b64 = base64.b64encode(buf.getvalue()).decode("utf-8")
    except Exception:
        # Fallback: send raw file
        with open(image_path, "rb") as f:
            image_b64 = base64.b64encode(f.read()).decode("utf-8")

    async with httpx.AsyncClient() as client:
        response = await client.post(
            f"{OLLAMA_URL}/api/chat",
            json={
                "model": VISION_MODEL,
                "messages": [
                    {
                        "role": "user",
                        "content": "/no_think " + prompt,
                        "images": [image_b64]
                    }
                ],
                "stream": False,
                "options": {"num_predict": 150}
            },
            timeout=120.0
        )

        if response.status_code != 200:
            return f"[Vision model error: {response.text}]"

        result = response.json()
        msg = result.get("message", {})
        # Some models put output in thinking field instead of content
        text = msg.get("content", "") or msg.get("thinking", "")
        return text if text else "[No description returned]"


async def call_claude(messages: list[dict], model: str = None, max_tokens: int = 1024) -> tuple[str, int, int]:
    """Call Anthropic Claude API. Returns (text, input_tokens, output_tokens)."""
    if not ANTHROPIC_API_KEY:
        raise HTTPException(status_code=503, detail="Anthropic API key not configured")

    model = model or CLAUDE_MODEL

    # Claude API uses a different format: system prompt is separate
    system_prompt = ""
    api_messages = []
    for msg in messages:
        if msg["role"] == "system":
            system_prompt = msg["content"]
        else:
            api_messages.append({"role": msg["role"], "content": msg["content"]})

    async with httpx.AsyncClient() as client:
        body = {
            "model": model,
            "max_tokens": max_tokens,
            "temperature": 0.7,
            "messages": api_messages,
        }
        if system_prompt:
            # Use prompt caching to avoid re-processing the system prompt every call
            body["system"] = [
                {
                    "type": "text",
                    "text": system_prompt,
                    "cache_control": {"type": "ephemeral"}
                }
            ]

        response = await client.post(
            "https://api.anthropic.com/v1/messages",
            headers={
                "x-api-key": ANTHROPIC_API_KEY,
                "anthropic-version": "2023-06-01",
                "anthropic-beta": "prompt-caching-2024-07-31",
                "Content-Type": "application/json",
            },
            json=body,
            timeout=60.0,
        )

        if response.status_code != 200:
            error_detail = response.text
            raise HTTPException(status_code=502, detail=f"Claude API error: {error_detail}")

        result = response.json()
        usage = result.get("usage", {})
        # Claude returns content as a list of blocks
        content_blocks = result.get("content", [])
        text = "".join(b.get("text", "") for b in content_blocks if b.get("type") == "text")
        return (
            text,
            usage.get("input_tokens", 0),
            usage.get("output_tokens", 0),
        )


async def call_deepseek(messages: list[dict], model: str = None) -> tuple[str, int, int]:
    """Call DeepSeek API — OpenAI compatible. Returns (text, input_tokens, output_tokens)."""
    if not DEEPSEEK_API_KEY:
        raise HTTPException(status_code=503, detail="DeepSeek API key not configured")

    model = model or DEEPSEEK_MODEL

    async with httpx.AsyncClient() as client:
        response = await client.post(
            DEEPSEEK_API_URL,
            headers={
                "Authorization": f"Bearer {DEEPSEEK_API_KEY}",
                "Content-Type": "application/json"
            },
            json={
                "model": model,
                "messages": messages,
                "temperature": 0.7,
                "max_tokens": 1024
            },
            timeout=60.0
        )

        if response.status_code != 200:
            error_detail = response.text
            raise HTTPException(status_code=502, detail=f"DeepSeek API error: {error_detail}")

        result = response.json()
        usage = result.get("usage", {})
        return (
            result["choices"][0]["message"]["content"],
            usage.get("prompt_tokens", 0),
            usage.get("completion_tokens", 0),
        )


async def call_provider(provider: str, messages: list[dict], model: str = None, max_tokens: int = 1024) -> tuple[str, str, int, int]:
    """Call the appropriate provider and return (response, model_used, input_tokens, output_tokens).
    Auto-falls back to ollama if primary is unavailable."""
    if provider == "deepseek":
        if not DEEPSEEK_API_KEY:
            print("[provider] DeepSeek API key not set, falling back to Ollama")
            provider = "ollama"
        else:
            model = model or DEEPSEEK_MODEL
            try:
                text, in_tok, out_tok = await call_deepseek(messages, model)
                return text, model, in_tok, out_tok
            except Exception as e:
                print(f"[provider] DeepSeek failed ({e}), falling back to Ollama")
                provider = "ollama"

    if provider == "claude":
        if not ANTHROPIC_API_KEY:
            print("[provider] Anthropic API key not set, falling back to Ollama")
            provider = "ollama"
        else:
            model = model or CLAUDE_MODEL
            try:
                text, in_tok, out_tok = await call_claude(messages, model, max_tokens=max_tokens)
                return text, model, in_tok, out_tok
            except Exception as e:
                print(f"[provider] Claude failed ({e}), falling back to Ollama")
                provider = "ollama"

    if provider == "grok":
        if not XAI_API_KEY:
            print("[provider] Grok API key not set, falling back to Ollama")
            provider = "ollama"
        else:
            model = model or GROK_MODEL
            try:
                text, in_tok, out_tok = await call_grok(messages, model)
                return text, model, in_tok, out_tok
            except Exception as e:
                print(f"[provider] Grok failed ({e}), falling back to Ollama")
                provider = "ollama"

    if provider == "ollama":
        model = model or OLLAMA_MODEL
        text, in_tok, out_tok = await call_ollama(messages, model)
        return text, model, in_tok, out_tok

    raise HTTPException(status_code=400, detail=f"Unknown provider: {provider}")


@app.get("/health")
async def health_check():
    """Check if server and providers are available."""
    status = {"status": "healthy", "providers": {}}
    
    # Check Grok
    if XAI_API_KEY:
        status["providers"]["grok"] = {"configured": True, "model": GROK_MODEL}
    else:
        status["providers"]["grok"] = {"configured": False}
    
    # Check Ollama
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.get(f"{OLLAMA_URL}/api/tags", timeout=2.0)
            if resp.status_code == 200:
                status["providers"]["ollama"] = {"connected": True, "model": OLLAMA_MODEL}
            else:
                status["providers"]["ollama"] = {"connected": False}
    except Exception:
        status["providers"]["ollama"] = {"connected": False}
    
    # Check Claude
    if ANTHROPIC_API_KEY:
        status["providers"]["claude"] = {"configured": True, "model": CLAUDE_MODEL}
    else:
        status["providers"]["claude"] = {"configured": False}

    # Check DeepSeek
    if DEEPSEEK_API_KEY:
        status["providers"]["deepseek"] = {"configured": True, "model": DEEPSEEK_MODEL}
    else:
        status["providers"]["deepseek"] = {"configured": False}

    status["default_provider"] = DEFAULT_PROVIDER
    status["tts_available"] = TTS_AVAILABLE
    status["stt_available"] = STT_AVAILABLE
    return status


@app.get("/providers")
async def list_providers():
    """List available providers and their status."""
    return await health_check()


@app.post("/session/new", response_model=SessionResponse)
async def create_session(request: NewSessionRequest):
    """Create a new conversation session."""
    session_id = str(uuid.uuid4())
    provider = request.provider or DEFAULT_PROVIDER
    model = GROK_MODEL if provider == "grok" else CLAUDE_MODEL if provider == "claude" else DEEPSEEK_MODEL if provider == "deepseek" else OLLAMA_MODEL

    system_prompt = build_system_prompt(
        request.npc_name,
        request.being_type,
        request.npc_personality
    )

    conversations[session_id] = {
        "messages": [{"role": "system", "content": system_prompt}],
        "provider": provider,
        "model": model,
        "npc_name": request.npc_name,
        "being_type": request.being_type,
        "total_input_tokens": 0,
        "total_output_tokens": 0,
    }

    return SessionResponse(session_id=session_id, provider=provider, model=model)


@app.get("/sessions/context")
async def get_sessions_context():
    """Get context size info for all active sessions (for debugging)."""
    result = {}
    for sid, session in conversations.items():
        msgs = session["messages"]
        total_chars = sum(len(m.get("content", "")) for m in msgs)
        # Rough token estimate: ~4 chars per token
        est_tokens = total_chars // 4
        result[sid] = {
            "npc_name": session.get("npc_name", "?"),
            "messages": len(msgs),
            "chars": total_chars,
            "est_tokens": est_tokens,
            "provider": session.get("provider", "?"),
            "total_input_tokens": session.get("total_input_tokens", 0),
            "total_output_tokens": session.get("total_output_tokens", 0),
        }
    return result


@app.get("/sessions/context/full")
async def get_sessions_context_full():
    """Get full message history for all active sessions (for debugging UI)."""
    result = {}
    for sid, session in conversations.items():
        msgs = session["messages"]
        result[sid] = {
            "npc_name": session.get("npc_name", "?"),
            "provider": session.get("provider", "?"),
            "messages": [
                {"role": m.get("role", "?"), "content": m.get("content", "")[:2000]}
                for m in msgs
            ],
        }
    return result


@app.post("/session/{session_id}/end")
async def end_session(session_id: str):
    """End and clean up a conversation session."""
    if session_id in conversations:
        del conversations[session_id]
        return {"status": "ended"}
    return {"status": "not_found"}


@app.post("/chat", response_model=ChatResponse)
async def chat(request: ChatRequest):
    """Send a message and get AI response."""
    
    provider = request.provider or DEFAULT_PROVIDER

    # Create session if needed
    if request.session_id is None or request.session_id not in conversations:
        session_id = str(uuid.uuid4())
        model = GROK_MODEL if provider == "grok" else CLAUDE_MODEL if provider == "claude" else DEEPSEEK_MODEL if provider == "deepseek" else OLLAMA_MODEL
        
        system_prompt = build_system_prompt(
            request.npc_name,
            request.being_type,
            request.npc_personality
        )

        conversations[session_id] = {
            "messages": [{"role": "system", "content": system_prompt}],
            "provider": provider,
            "model": model,
            "npc_name": request.npc_name,
            "being_type": request.being_type
        }
    else:
        session_id = request.session_id
        # Allow provider override per-message
        if request.provider:
            conversations[session_id]["provider"] = request.provider

    session = conversations[session_id]
    
    # If an image path was provided, get a vision model description first
    vision_description = ""
    if request.image_path:
        img_prompt = "Describe what you see in this image. Be specific about colors, objects, shapes, and style. Keep it concise."
        try:
            vision_description = await call_vision(request.image_path, img_prompt)
            print(f"[vision] Described {request.image_path}: {vision_description[:100]}...")
        except Exception as e:
            vision_description = f"[Vision model unavailable: {e}]"
            print(f"[vision] Error: {e}")

    # Build user message content, optionally with perception context
    user_content = request.message
    if vision_description:
        # When vision is active, put the description front and center and skip perception noise
        user_content = (
            f"The player selected an image file and asked you to describe it.\n"
            f"A vision model analyzed the actual image and saw: {vision_description}\n\n"
            f"Relay this description to the player in your own words. "
            f"Do NOT describe the scene around you — only describe the image contents.\n\n"
            f"Player said: {request.message}"
        )
        # Skip perception for vision requests — it just confuses the response
        request.perception = None
    if request.perception:
        p = request.perception
        context_parts = []

        # NPC's own position (always include — cheap, enables movement decisions)
        pos = p.get("position", [])
        if pos and len(pos) >= 3:
            context_parts.append(f"[Your position: ({pos[0]:.1f}, {pos[1]:.1f}, {pos[2]:.1f})]")

        # Player position (always include — cheap, enables relative movement)
        ppos = p.get("player_position", [])
        if ppos and len(ppos) >= 3:
            context_parts.append(f"[Player position: ({ppos[0]:.1f}, {ppos[1]:.1f}, {ppos[2]:.1f})]")

        # Visible objects: name + distance only, deduplicated, integer distances
        visible = p.get("visible_objects", [])
        if visible:
            seen_names = set()
            deduped = []
            for o in visible[:20]:
                name = o.get("name", "?")
                if name not in seen_names:
                    seen_names.add(name)
                    deduped.append(f"{name} ({int(round(o.get('distance', 0)))}m)")
            obj_list = ", ".join(deduped)
            context_parts.append(f"[You can see: {obj_list}]")

        if context_parts:
            user_content = "\n".join(context_parts) + f"\n\n{request.message}"

    # Add user message to history
    session["messages"].append({
        "role": "user",
        "content": user_content
    })

    try:
        # Call the appropriate provider
        response_text, model_used, in_tok, out_tok = await call_provider(
            session["provider"],
            session["messages"],
            session.get("model")
        )
        session["total_input_tokens"] = session.get("total_input_tokens", 0) + in_tok
        session["total_output_tokens"] = session.get("total_output_tokens", 0) + out_tok

        # Parse action from response (if any)
        clean_text, action = parse_action_from_response(response_text)

        # Parse emotion tag from response
        clean_text, emotion = parse_emotion(clean_text)

        # Add assistant response to history (strip think tags to save context space)
        session["messages"].append({
            "role": "assistant",
            "content": strip_think_tags(response_text)
        })

        # Summarize old messages to reduce token usage (triggers at 24+ messages)
        if len(session["messages"]) > 24:
            session["messages"] = _summarize_old_messages(session["messages"], keep_recent=12)

        return ChatResponse(
            session_id=session_id,
            response=clean_text,
            provider=session["provider"],
            model=model_used,
            action=action,
            emotion=emotion
        )

    except httpx.TimeoutException:
        raise HTTPException(status_code=504, detail="Provider timeout")
    except httpx.ConnectError:
        raise HTTPException(status_code=503, detail="Cannot connect to provider")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/models")
async def list_models():
    """List available models from all providers."""
    models = {
        "grok": [GROK_MODEL] if XAI_API_KEY else [],
    }
    
    # Get Ollama models
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.get(f"{OLLAMA_URL}/api/tags", timeout=5.0)
            if resp.status_code == 200:
                data = resp.json()
                models["ollama"] = [m["name"] for m in data.get("models", [])]
    except Exception:
        models["ollama"] = []
    
    return models


class SwitchModelRequest(BaseModel):
    model: str  # e.g. "qwen3.5:9b" or "qwen3:30b"


@app.post("/model/switch")
async def switch_ollama_model(request: SwitchModelRequest):
    """Switch the active Ollama model for all new sessions."""
    global OLLAMA_MODEL

    # Verify the model exists in Ollama
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.get(f"{OLLAMA_URL}/api/tags", timeout=5.0)
            if resp.status_code == 200:
                available = [m["name"] for m in resp.json().get("models", [])]
                if request.model not in available:
                    return {
                        "status": "error",
                        "message": f"Model '{request.model}' not found. Available: {available}",
                    }
    except Exception:
        return {"status": "error", "message": "Cannot connect to Ollama"}

    old_model = OLLAMA_MODEL
    OLLAMA_MODEL = request.model

    # Update existing sessions that use ollama
    updated = 0
    for session in conversations.values():
        if session.get("provider") == "ollama":
            session["model"] = request.model
            updated += 1

    return {
        "status": "ok",
        "old_model": old_model,
        "new_model": OLLAMA_MODEL,
        "sessions_updated": updated,
    }


class SwitchProviderRequest(BaseModel):
    provider: str  # "ollama" or "grok"


@app.post("/provider/switch")
async def switch_provider(request: SwitchProviderRequest):
    """Switch the active provider for all new sessions."""
    global DEFAULT_PROVIDER

    if request.provider not in ("ollama", "grok", "claude", "deepseek"):
        return {"status": "error", "message": f"Unknown provider: {request.provider}"}

    if request.provider == "grok" and not XAI_API_KEY:
        return {"status": "error", "message": "Grok API key not configured"}

    if request.provider == "claude" and not ANTHROPIC_API_KEY:
        return {"status": "error", "message": "Anthropic API key not configured"}

    if request.provider == "deepseek" and not DEEPSEEK_API_KEY:
        return {"status": "error", "message": "DeepSeek API key not configured"}

    old = DEFAULT_PROVIDER
    DEFAULT_PROVIDER = request.provider

    # Update existing sessions
    updated = 0
    new_model = GROK_MODEL if request.provider == "grok" else CLAUDE_MODEL if request.provider == "claude" else DEEPSEEK_MODEL if request.provider == "deepseek" else OLLAMA_MODEL
    for session in conversations.values():
        session["provider"] = request.provider
        session["model"] = new_model
        updated += 1

    return {
        "status": "ok",
        "old_provider": old,
        "new_provider": DEFAULT_PROVIDER,
        "model": new_model,
        "sessions_updated": updated,
    }


@app.get("/model/current")
async def get_current_model():
    """Get the currently active model and provider."""
    return {"ollama_model": OLLAMA_MODEL, "grok_model": GROK_MODEL, "claude_model": CLAUDE_MODEL, "deepseek_model": DEEPSEEK_MODEL, "provider": DEFAULT_PROVIDER}


class HeartbeatRequest(BaseModel):
    session_id: Optional[str] = None
    npc_name: str = "NPC"
    being_type: int = 1
    perception: Optional[dict] = None


@app.post("/heartbeat")
async def heartbeat(request: HeartbeatRequest):
    """Periodic passive perception — NPC observes surroundings and may react."""
    provider = DEFAULT_PROVIDER
    session_id = request.session_id or ""

    # Build perception context (deduplicated, integer distances)
    perception_text = ""
    visible = []
    if request.perception:
        visible = request.perception.get("visible_objects", [])
        if visible:
            seen_names = set()
            deduped = []
            for o in visible[:20]:
                name = o.get("name", "?")
                if name not in seen_names:
                    seen_names.add(name)
                    dist = int(round(o.get("distance", 0)))
                    bearing = o.get("bearing", "")
                    deduped.append(f"{name} ({o.get('type', '?')}, {dist}m {bearing})")
            obj_list = ", ".join(deduped)
            perception_text = f"[You can see: {obj_list}]"

    # Get or create session
    if session_id and session_id in conversations:
        session = conversations[session_id]
    else:
        session_id = str(uuid.uuid4())
        system_prompt = build_system_prompt(request.npc_name, request.being_type)
        model = GROK_MODEL if provider == "grok" else CLAUDE_MODEL if provider == "claude" else DEEPSEEK_MODEL if provider == "deepseek" else OLLAMA_MODEL
        session = {
            "messages": [{"role": "system", "content": system_prompt}],
            "provider": provider,
            "model": model,
            "npc_name": request.npc_name,
            "being_type": request.being_type,
            "last_perception": "",
            "total_input_tokens": 0,
            "total_output_tokens": 0,
        }
        conversations[session_id] = session

    # Compare to last perception — only query LLM if visible objects actually changed
    # (ignore position/distance changes which happen constantly as player moves)
    perception_signature = _strip_positions_for_comparison(perception_text)
    if perception_signature == session.get("last_perception_sig", ""):
        return {"status": "ok", "session_id": session_id}
    session["last_perception_sig"] = perception_signature

    # Slim heartbeat message — instructions are in system prompt
    heartbeat_msg = f"[HEARTBEAT]\n{perception_text}"

    session["messages"].append({"role": "user", "content": heartbeat_msg})

    try:
        response_text, model_used, in_tok, out_tok = await call_provider(
            session["provider"], session["messages"], session.get("model"),
            max_tokens=256  # Heartbeats should be short — save tokens
        )
        session["total_input_tokens"] = session.get("total_input_tokens", 0) + in_tok
        session["total_output_tokens"] = session.get("total_output_tokens", 0) + out_tok

        clean_response = strip_think_tags(response_text)

        # Parse action
        clean_text, action = parse_action_from_response(response_text)

        # Parse emotion tag first so NOTHING check isn't blocked by [neutral] prefix
        clean_text, emotion = parse_emotion(clean_text)

        # If AI said NOTHING, collapse into previous heartbeat instead of storing
        if clean_text.strip().upper() == "NOTHING" or clean_text.strip() == "":
            # Remove the heartbeat user message we just added (no useful info gained)
            session["messages"].pop()
            return {"status": "ok", "session_id": session_id}

        # Only store heartbeat exchanges that produced actual responses
        session["messages"].append({"role": "assistant", "content": clean_response})

        # Summarize old heartbeat messages to reduce token usage
        if len(session["messages"]) > 20:
            session["messages"] = _summarize_old_messages(session["messages"], keep_recent=8)

        result = {
            "status": "ok",
            "session_id": session_id,
            "response": clean_text,
            "emotion": emotion,
            "changes_detected": True,
        }
        if action:
            result["action"] = action
        return result

    except Exception as e:
        print(f"[heartbeat] Error: {e}")
        return {"status": "ok", "session_id": session_id}


class TTSRequest(BaseModel):
    text: str
    voice: str = "en-US-GuyNeural"
    rate: str = "+0%"
    robot: bool = False


@app.post("/tts")
async def text_to_speech(request: TTSRequest):
    """Convert text to speech using edge-tts."""
    if not TTS_AVAILABLE:
        raise HTTPException(status_code=503, detail="edge-tts not installed. Run: pip install edge-tts")

    try:
        voice = request.voice
        rate = request.rate
        if request.robot:
            rate = "+20%"

        communicate = edge_tts.Communicate(request.text, voice, rate=rate)

        audio_data = io.BytesIO()
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                audio_data.write(chunk["data"])

        return Response(content=audio_data.getvalue(), media_type="audio/mpeg")

    except Exception as e:
        raise HTTPException(status_code=500, detail=f"TTS error: {str(e)}")


@app.post("/stt")
async def speech_to_text(audio: UploadFile = File(...)):
    """Transcribe audio using OpenAI Whisper API or local faster-whisper."""
    if not STT_AVAILABLE:
        raise HTTPException(
            status_code=503,
            detail="STT not available. Install: pip install faster-whisper (local) "
                   "or set OPENAI_API_KEY for cloud Whisper."
        )

    try:
        # Save uploaded audio to temp file
        suffix = ".wav"
        if audio.filename and "." in audio.filename:
            suffix = "." + audio.filename.rsplit(".", 1)[1]

        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as tmp:
            content = await audio.read()
            tmp.write(content)
            tmp_path = tmp.name

        try:
            if STT_MODE == "openai":
                client = openai.OpenAI(api_key=OPENAI_API_KEY)
                with open(tmp_path, "rb") as audio_file:
                    transcript = client.audio.transcriptions.create(
                        model="whisper-1",
                        file=audio_file
                    )
                return {"text": transcript.text}
            else:
                # Local faster-whisper
                global _whisper_model
                if _whisper_model is None:
                    print("[STT] Loading faster-whisper model (base.en)...")
                    _whisper_model = WhisperModel("base.en", compute_type="int8")
                    print("[STT] Model loaded.")

                segments, _ = _whisper_model.transcribe(tmp_path, beam_size=3)
                text = " ".join(seg.text.strip() for seg in segments)
                return {"text": text}
        finally:
            os.unlink(tmp_path)

    except Exception as e:
        raise HTTPException(status_code=500, detail=f"STT error: {str(e)}")


if __name__ == "__main__":
    import uvicorn

    print("=" * 50)
    print("  EDEN AI Backend Server v0.2.0")
    print("=" * 50)
    print(f"Default Provider: {DEFAULT_PROVIDER}")
    print(f"Grok Model: {GROK_MODEL}")
    print(f"Grok API Key: {'configured' if XAI_API_KEY else 'NOT SET'}")
    print(f"Claude Model: {CLAUDE_MODEL}")
    print(f"Claude API Key: {'configured' if ANTHROPIC_API_KEY else 'NOT SET'}")
    print(f"DeepSeek Model: {DEEPSEEK_MODEL}")
    print(f"DeepSeek API Key: {'configured' if DEEPSEEK_API_KEY else 'NOT SET'}")
    print(f"Ollama URL: {OLLAMA_URL}")
    print(f"Ollama Model: {OLLAMA_MODEL}")
    print(f"TTS (edge-tts): {'available' if TTS_AVAILABLE else 'NOT INSTALLED (pip install edge-tts)'}")
    print(f"STT ({STT_MODE or 'none'}):  {'available' if STT_AVAILABLE else 'NOT AVAILABLE (pip install faster-whisper)'}")
    print("=" * 50)
    print("Starting server on http://localhost:8080")
    print()
    
    uvicorn.run(app, host="0.0.0.0", port=8080)
