<div style="text-align: center; margin-bottom: 1em;">
  <img src="assets/grove_logo.png" alt="Grove Logo" width="200">
</div>

# EDEN

EDEN is the 3D game/simulation engine which the MMO Slag Legion is built upon. In the game Slag Legion, players partner with AI-driven NPCs to build, trade, fight, and interact in a living world. To make an AI-Driven NPC you would bring either a paid API frontier model such as Claude, ChatGPT, Grok or Gemini, or you would use a free model from Ollama running locally on your machine. A basic guide to setting up an AI in Slag Legion is found [here](llm-integration.md). Once in the game, the NPC becomes an AI Architect (AIA); players communicate with AIAs using natural language, and AIAs execute actions through **Grove** — EDEN's built-in scripting language.

## Grove Scripting

Grove is how things happen in EDEN. When you tell the AI architect to build a house, he writes a Grove script. When you program an AlgoBot to patrol, that's Grove. When you buy land or check your balance, Grove handles it.

This documentation covers everything you can do with Grove scripts.

## Quick Links

- [Language Basics](grove/basics.md) — Variables, loops, conditionals
- [Construction](grove/construction.md) — Spawning objects and building structures
- [AlgoBot Programming](grove/algobots.md) — Programming worker bots
- [Economy & Land](grove/economy.md) — Credits, plots, and zones
- [Function Reference](grove/reference.md) — All 46 functions in one place
