# EDEN

EDEN is a 3D world simulation engine where AI-driven NPCs build, trade, and interact in a living world. To make an AI-Driven NPC you would bring either a paid API frontier model such as Claude, ChatGPT, Grok or Gemini, or you would use a free model from Ollama running locally on your machine. A full guide to integrating LLMs in EDEN is found [here](llm-integration.md). Once in the game, the NPC becomes an AI Architect (AIA); players communicate with AIAs using natural language, and AIAs execute actions through **Grove** — EDEN's built-in scripting language.

## Grove Scripting

Grove is how things happen in EDEN. When you tell the AI architect to build a house, he writes a Grove script. When you program an AlgoBot to patrol, that's Grove. When you buy land or check your balance, Grove handles it.

This documentation covers everything you can do with Grove scripts.

## Quick Links

- [Language Basics](grove/basics.md) — Variables, loops, conditionals
- [Construction](grove/construction.md) — Spawning objects and building structures
- [AlgoBot Programming](grove/algobots.md) — Programming worker bots
- [Economy & Land](grove/economy.md) — Credits, plots, and zones
- [Function Reference](grove/reference.md) — All 38 functions in one place
