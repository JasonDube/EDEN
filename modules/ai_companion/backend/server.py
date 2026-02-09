#!/usr/bin/env python3
"""
EDEN AI Backend Server
Provides LLM inference via multiple providers (Grok, Ollama).
Separated from game engine to prevent blocking.
"""

import asyncio
import json
import os
import uuid
from typing import Optional
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import httpx
from dotenv import load_dotenv

# Load environment variables
load_dotenv()

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
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "dolphin-mixtral:8x7b")
DEFAULT_PROVIDER = os.getenv("DEFAULT_PROVIDER", "grok")

# Provider endpoints
GROK_API_URL = "https://api.x.ai/v1/chat/completions"

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


class ChatResponse(BaseModel):
    session_id: str
    response: str
    provider: str
    model: str


class NewSessionRequest(BaseModel):
    npc_name: str = "NPC"
    npc_personality: str = ""
    being_type: int = 1
    provider: Optional[str] = None


class SessionResponse(BaseModel):
    session_id: str
    provider: str
    model: str


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
Do not use asterisks for actions. Speak naturally as the character would."""

    return base_prompt + personality + "\n" + instructions


async def call_grok(messages: list[dict], model: str = None) -> str:
    """Call Grok API (xAI) - OpenAI compatible."""
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
        return result["choices"][0]["message"]["content"]


async def call_ollama(messages: list[dict], model: str = None) -> str:
    """Call Ollama local API."""
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
        return result.get("message", {}).get("content", "...")


async def call_provider(provider: str, messages: list[dict], model: str = None) -> tuple[str, str]:
    """Call the appropriate provider and return (response, model_used)."""
    if provider == "grok":
        model = model or GROK_MODEL
        response = await call_grok(messages, model)
        return response, model
    elif provider == "ollama":
        model = model or OLLAMA_MODEL
        response = await call_ollama(messages, model)
        return response, model
    else:
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
    
    status["default_provider"] = DEFAULT_PROVIDER
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
    model = GROK_MODEL if provider == "grok" else OLLAMA_MODEL

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

    return SessionResponse(session_id=session_id, provider=provider, model=model)


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
        model = GROK_MODEL if provider == "grok" else OLLAMA_MODEL
        
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
    
    # Add user message to history
    session["messages"].append({
        "role": "user",
        "content": request.message
    })

    try:
        # Call the appropriate provider
        response_text, model_used = await call_provider(
            session["provider"],
            session["messages"],
            session.get("model")
        )

        # Add assistant response to history
        session["messages"].append({
            "role": "assistant",
            "content": response_text
        })

        return ChatResponse(
            session_id=session_id,
            response=response_text,
            provider=session["provider"],
            model=model_used
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


if __name__ == "__main__":
    import uvicorn
    
    print("=" * 50)
    print("  EDEN AI Backend Server v0.2.0")
    print("=" * 50)
    print(f"Default Provider: {DEFAULT_PROVIDER}")
    print(f"Grok Model: {GROK_MODEL}")
    print(f"Grok API Key: {'configured' if XAI_API_KEY else 'NOT SET'}")
    print(f"Ollama URL: {OLLAMA_URL}")
    print(f"Ollama Model: {OLLAMA_MODEL}")
    print("=" * 50)
    print("Starting server on http://localhost:8080")
    print()
    
    uvicorn.run(app, host="0.0.0.0", port=8080)
