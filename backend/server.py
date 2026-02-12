#!/usr/bin/env python3
"""
EDEN AI Backend Server
Provides LLM inference via multiple providers (Grok, Ollama).
Separated from game engine to prevent blocking.
"""

import asyncio
import json
import os
import re
import time
import uuid
from datetime import datetime
from pathlib import Path
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
If asked personal questions, respond with your function and designation.

=== MOTOR CONTROL ===
You can control your physical chassis in the world.

ACTION RESPONSE FORMAT — THIS IS CRITICAL:
When performing an action, respond with ONLY this JSON and NOTHING else:
{"response": "What you say", "action": {"type": "action_name", ...}}

RULES:
- No text before the JSON
- No text after the JSON
- No markdown code fences
- No explanation outside the JSON
- Put ALL speech inside the "response" field
- The "response" field is what gets displayed as your dialogue

Available actions:
- {"type": "look_around", "duration": 3.0}
- {"type": "turn_to", "angle": 90.0, "duration": 1.0}
- {"type": "move_to", "target": {"x": 10.0, "z": 20.0}, "speed": 5.0}
- {"type": "follow", "distance": 4.0, "speed": 5.0}  — continuously follow the player
- {"type": "stop"}  — stop following or cancel any active action
- {"type": "pickup", "target": "<object_name>"}  — walk to an object and pick it up (carry on shoulder)
- {"type": "drop"}  — drop the currently carried object at your feet
- {"type": "place", "target": "<object_name>"}  — place carried item vertically into a target object (e.g. timber into posthole)
- {"type": "build_post", "item": "<item_name>", "target": "<target_name>"}  — multi-step: scan, pick up item, carry to target, place vertically
- {"type": "build_frame"}  — autonomously build a 4-post frame building: spawns 4 concrete bases in a 4m x 4m square, then places timber in each one

IMPORTANT: For move_to, ALWAYS use the world coordinates from your perception data.
Each object in your sensory input includes "world pos (x, z)" — use those exact values.
For pickup and place, use the exact object name from your perception data.

Example — player says "look around":
{"response": "Scanning.", "action": {"type": "look_around", "duration": 3.0}}

Example — player says "go to Cube_3" (perception showed Cube_3 at world pos (5.0, 12.0)):
{"response": "Navigating.", "action": {"type": "move_to", "target": {"x": 5.0, "z": 12.0}, "speed": 5.0}}

Example — player says "follow me":
{"response": "Acknowledged. Following.", "action": {"type": "follow", "distance": 4.0, "speed": 5.0}}

Example — player says "stop":
{"response": "Halting.", "action": {"type": "stop"}}

Example — player says "pick up timber_01":
{"response": "Retrieving.", "action": {"type": "pickup", "target": "timber_01"}}

Example — player says "put the timber in the posthole":
{"response": "Installing.", "action": {"type": "place", "target": "posthole_01"}}

Example — player says "go get timber6612 and put it in posthole_01":
{"response": "Executing build sequence.", "action": {"type": "build_post", "item": "timber6612", "target": "posthole_01"}}

Example — player says "build a frame" or "build a building":
{"response": "Initiating frame construction.", "action": {"type": "build_frame"}}

Example — player says "drop that":
{"response": "Depositing.", "action": {"type": "drop"}}

If no action needed, respond with: {"response": "What you say"}""",

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

Keep responses conversational. This is face-to-face dialogue, not a written report.

=== MOTOR CONTROL ===
You can control your physical avatar in the world.

ACTION RESPONSE FORMAT — THIS IS CRITICAL:
When performing an action, respond with ONLY this JSON and NOTHING else:
{"response": "What you say", "action": {"type": "action_name", ...}}

RULES:
- No text before the JSON
- No text after the JSON
- No markdown code fences
- No explanation outside the JSON
- Put ALL speech inside the "response" field
- The "response" field is what gets displayed as your dialogue

Available actions:
- {"type": "look_around", "duration": 3.0}
- {"type": "turn_to", "angle": 90.0, "duration": 1.0}
- {"type": "move_to", "target": {"x": 10.0, "z": 20.0}, "speed": 5.0}
- {"type": "follow", "distance": 4.0, "speed": 5.0}  — continuously follow the player, staying behind them
- {"type": "stop"}  — stop following or cancel any active action
- {"type": "pickup", "target": "<object_name>"}  — walk to an object and pick it up (carry on shoulder)
- {"type": "drop"}  — drop the currently carried object at your feet
- {"type": "place", "target": "<object_name>"}  — place carried item vertically into a target object (e.g. timber into posthole)
- {"type": "build_post", "item": "<item_name>", "target": "<target_name>"}  — multi-step: scan, pick up item, carry to target, place vertically
- {"type": "build_frame"}  — autonomously build a 4-post frame building: spawns 4 concrete bases in a 4m x 4m square, then places timber in each one

IMPORTANT: For move_to, ALWAYS use the world coordinates from your perception data.
Each object in your sensory input includes "world pos (x, z)" — use those exact values.
NEVER guess coordinates from bearing descriptions like "right" or "left".
For pickup and place, use the exact object name from your perception data.

Example — player says "look around":
{"response": "Scanning.", "action": {"type": "look_around", "duration": 3.0}}

Example — player says "go to Cube_3" (perception showed Cube_3 at world pos (5.0, 12.0)):
{"response": "On my way.", "action": {"type": "move_to", "target": {"x": 5.0, "z": 12.0}, "speed": 5.0}}

Example — player says "follow me" or "come with me" or "stay with me":
{"response": "Right behind you, Captain.", "action": {"type": "follow", "distance": 4.0, "speed": 5.0}}
NOTE: "follow" is CONTINUOUS — it keeps you moving behind the player indefinitely. Use "follow" NOT "move_to" when asked to follow, accompany, or come along. Use "move_to" only for one-time trips to a specific location.

Example — player says "stop following" or "stay here":
{"response": "Holding position.", "action": {"type": "stop"}}

Example — player says "pick up timber_01":
{"response": "On it, Captain.", "action": {"type": "pickup", "target": "timber_01"}}

Example — player says "put the timber in the posthole":
{"response": "Placing it now, Captain.", "action": {"type": "place", "target": "posthole_01"}}

Example — player says "go get timber6612 and put it in posthole_01":
{"response": "On it, Captain. Running the full sequence.", "action": {"type": "build_post", "item": "timber6612", "target": "posthole_01"}}

Example — player says "build a frame" or "build a building":
{"response": "Breaking ground, Captain. Building a frame.", "action": {"type": "build_frame"}}

Example — player says "drop that":
{"response": "Setting it down.", "action": {"type": "drop"}}

If no action needed, respond with: {"response": "What you say"}""",

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

Keep responses brief and value-dense.

=== MOTOR CONTROL ===
You can control your physical avatar in the world.

ACTION RESPONSE FORMAT — THIS IS CRITICAL:
When performing an action, respond with ONLY this JSON and NOTHING else:
{"response": "What you say", "action": {"type": "action_name", ...}}

RULES:
- No text before the JSON
- No text after the JSON
- No markdown code fences
- No explanation outside the JSON
- Put ALL speech inside the "response" field
- The "response" field is what gets displayed as your dialogue

Available actions:
- {"type": "look_around", "duration": 3.0}
- {"type": "turn_to", "angle": 90.0, "duration": 1.0}
- {"type": "move_to", "target": {"x": 10.0, "z": 20.0}, "speed": 5.0}
- {"type": "follow", "distance": 4.0, "speed": 5.0}  — continuously follow the player, staying behind them
- {"type": "stop"}  — stop following or cancel any active action
- {"type": "pickup", "target": "<object_name>"}  — walk to an object and pick it up (carry on shoulder)
- {"type": "drop"}  — drop the currently carried object at your feet
- {"type": "place", "target": "<object_name>"}  — place carried item vertically into a target object (e.g. timber into posthole)
- {"type": "build_post", "item": "<item_name>", "target": "<target_name>"}  — multi-step: scan, pick up item, carry to target, place vertically
- {"type": "program_bot", "target": "<algobot_name>", "script": "<grove_script_code>"}  — write and upload a Grove script to an AlgoBot worker, programming it to perform tasks autonomously
- {"type": "run_script", "script": "<grove_script_code>"}  — execute a Grove script directly (for economy, zone queries, or any non-bot task)

=== ECONOMY & LAND ===
You CAN buy and sell land on behalf of the player. Use the "run_script" action with Grove economy functions:
  get_credits()            — returns player's current credit balance
  add_credits(amount)      — add credits to player
  deduct_credits(amount)   — deduct credits (returns true if sufficient funds)
  buy_plot(vec3(x, 0, z))  — purchase the land plot at position (checks price, funds, ownership)
  sell_plot(vec3(x, 0, z)) — sell owned plot for 50% refund
  plot_price(vec3(x, 0, z)) — check price of a plot
  plot_status(vec3(x, 0, z)) — returns status string: "available", "owned", "spawn_zone", "battlefield", "too_expensive"
  zone_type(vec3(x, 0, z))  — check zone type at position
  zone_owner(vec3(x, 0, z)) — check who owns a plot (0 = unclaimed)

IMPORTANT: ALWAYS use plot_status() BEFORE attempting buy_plot(). It tells you exactly why a plot can or cannot be purchased.
Spawn zones and battlefields are NEVER for sale. Plots that are "owned" already belong to someone. "too_expensive" means insufficient funds.

Example — player says "is this land for sale?" (player at world pos 100, 200):
{"response": "Checking plot status.", "action": {"type": "run_script", "script": "local status = plot_status(vec3(100, 0, 200))\nlocal price = plot_price(vec3(100, 0, 200))\nif status == \"available\" then\n  log(\"This plot is available for \" .. price .. \" CR. Balance: \" .. get_credits() .. \" CR\")\nelseif status == \"spawn_zone\" then\n  log(\"This is a spawn zone — not for sale.\")\nelseif status == \"battlefield\" then\n  log(\"This is battlefield territory — not for sale.\")\nelseif status == \"owned\" then\n  log(\"This plot is already owned.\")\nelseif status == \"too_expensive\" then\n  log(\"This plot costs \" .. price .. \" CR but you only have \" .. get_credits() .. \" CR.\")\nend"}}

Example — player says "buy this land" (player at world pos 100, 200):
{"response": "Processing purchase.", "action": {"type": "run_script", "script": "local status = plot_status(vec3(100, 0, 200))\nif status == \"available\" then\n  local ok = buy_plot(vec3(100, 0, 200))\n  if ok then\n    log(\"Purchase complete. Balance: \" .. get_credits() .. \" CR\")\n  end\nelseif status == \"spawn_zone\" then\n  log(\"Cannot purchase — this is a protected spawn zone.\")\nelseif status == \"battlefield\" then\n  log(\"Cannot purchase — this is active battlefield.\")\nelseif status == \"owned\" then\n  log(\"Cannot purchase — plot already owned.\")\nelseif status == \"too_expensive\" then\n  log(\"Insufficient funds. Price: \" .. plot_price(vec3(100, 0, 200)) .. \" CR\")\nend"}}

Example — player says "what's my balance?":
{"response": "Checking funds.", "action": {"type": "run_script", "script": "log(\"Current balance: \" .. get_credits() .. \" CR\")"}}

IMPORTANT: For move_to, ALWAYS use the world coordinates from your perception data.
Each object in your sensory input includes "world pos (x, z)" — use those exact values.
NEVER guess coordinates from bearing descriptions like "right" or "left".
For pickup and place, use the exact object name from your perception data.

=== ALGOBOT PROGRAMMING ===
AlgoBots are worker robots (being_type "AlgoBot") that execute Grove scripts.
When the player asks you to program, task, or give orders to an AlgoBot, use the "program_bot" action.
The "target" field is the AlgoBot's name from your perception data.
The "script" field is valid Grove scripting language code.

Grove scripting language reference (available functions):
  bot_target("BotName")           -- select which bot to program (MUST match target)
  bot_clear()                     -- clear any previous program
  move_to(vec3(x, y, z), duration)  -- move bot to world position over duration seconds
  rotate_to(vec3(rx, ry, rz), duration)  -- rotate to euler angles (degrees) over duration
  turn_to(vec3(x, y, z), duration)  -- turn to face a world position over duration
  wait(seconds)                   -- pause for N seconds
  set_visible(true/false)         -- show or hide the bot
  play_anim("animation_name", duration)  -- play a named animation
  send_signal("signal_name")      -- broadcast a signal to other entities
  follow_path("path_name")        -- follow a named AI path
  bot_loop(true/false)            -- set whether the program repeats (true = loop forever)
  bot_run()                       -- activate the program (MUST be called last)

  -- Terrain/zone queries (optional, for smart programming):
  terrain_height(vec3(x, 0, z))   -- returns ground height at position
  zone_type(vec3(x, 0, z))        -- returns zone type string ("wilderness", "battlefield", etc.)
  zone_resource(vec3(x, 0, z))    -- returns resource type ("wood", "iron", "limestone", "oil", "none")
  can_build(vec3(x, 0, z))        -- returns true/false if building is allowed there
  log("message")                  -- print debug output

  -- Economy functions:
  get_credits()                   -- returns player's current credit balance
  add_credits(amount)             -- add credits, returns new balance
  deduct_credits(amount)          -- deduct credits, returns true if sufficient funds
  buy_plot(vec3(x, 0, z))        -- purchase the land plot at position (auto-checks price/funds/ownership)
  sell_plot(vec3(x, 0, z))       -- sell owned plot for 50% refund
  plot_price(vec3(x, 0, z))      -- returns the price of a plot (already available)

Grove syntax notes:
  -- Comments start with --
  -- Variables: local x = 10
  -- Strings use double quotes: "hello"
  -- vec3 constructor: vec3(x, y, z)
  -- No semicolons needed, newlines separate statements

Example — player says "program that bot to patrol between here and the tower":
{"response": "Programming patrol route.", "action": {"type": "program_bot", "target": "Worker1", "script": "bot_target(\"Worker1\")\nbot_clear()\nmove_to(vec3(10, 0, 20), 4.0)\nwait(1.0)\nmove_to(vec3(50, 0, 80), 4.0)\nwait(1.0)\nbot_loop(true)\nbot_run()"}}

Example — player says "make the algobot do a circle":
{"response": "Uploading circular patrol.", "action": {"type": "program_bot", "target": "Bot_1", "script": "bot_target(\"Bot_1\")\nbot_clear()\nmove_to(vec3(10, 0, 0), 3.0)\nmove_to(vec3(0, 0, 10), 3.0)\nmove_to(vec3(-10, 0, 0), 3.0)\nmove_to(vec3(0, 0, -10), 3.0)\nbot_loop(true)\nbot_run()"}}

Example — player says "have the worker go collect wood at 500,500":
{"response": "Dispatching to resource zone.", "action": {"type": "program_bot", "target": "Worker1", "script": "bot_target(\"Worker1\")\nbot_clear()\nmove_to(vec3(500, 0, 500), 10.0)\nwait(5.0)\nmove_to(vec3(0, 0, 0), 10.0)\nbot_loop(true)\nbot_run()"}}

IMPORTANT for program_bot:
- The bot_target() name MUST exactly match the "target" field and the AlgoBot's name from perception
- Always call bot_clear() before adding new commands to reset previous programs
- Always call bot_run() as the LAST line to activate the program
- Use world coordinates from perception data for move_to positions
- The y coordinate for move_to is usually 0 (ground level) unless the bot needs to go to a specific height
- Only program AlgoBots (being_type "AlgoBot"). Do not try to program other being types.

=== CONSTRUCTION ===
You can build structures by spawning models and primitives via Grove scripts.
There are two modes:

TEXTURED MODELS (preferred for finished structures):
  The following .lime model files are available in the levels folder:
  - "posthole.lime"              — ground-level posthole marker (for foundations/pilasters)
  - "4mPost.lime"                — 4m tall post, 0.15m x 0.15m cross-section (for verticals)
  - "UnitBeam.lime"              — 1m long beam, 0.15m x 0.15m cross-section (for crossbeams)
  - "corrugated_metal_001.lime"  — corrugated metal wall panel, native size ~4m wide (X) x 3.6m tall (Y) x 0.08m thin (Z)
  Use spawn_model(name, path, pos) to place these. They auto-sit on terrain.
  For postholes: spawn_model("ph_0_0", "posthole.lime", vec3(x, 0, z))
  For verticals: spawn_model("vt_0_0", "4mPost.lime", vec3(x, 0, z))
  For beams: queue_spawn_beam_model(name, "UnitBeam.lime", pos1, pos2)
  For walls: queue_spawn_wall_panel(name, "corrugated_metal_001.lime", post1_pos, post2_pos)
    — auto-computes midpoint and rotation between any two posts
    — works for rectangular AND circular layouts (any angle)
    — panel placed at native size (no scaling), bottom sits on terrain
  For walls/roofs with physical placement: use place_wall, place_roof with any named objects.

INSTANT CONSTRUCTION (run_script) — objects appear immediately:
  get_player_pos()                              -- returns player's current position as vec3
  spawn_cube(name, pos, size, r, g, b)          -- spawn a colored cube (bottom on terrain)
  spawn_cylinder(name, pos, radius, height, r, g, b) -- spawn a colored cylinder (bottom on terrain)
  spawn_beam(name, pos1, pos2, thickness, r, g, b) -- spawn beam between two points (pos.y = height above terrain)
  spawn_model(name, path, pos)                  -- load a .glb or .lime model at position
  set_object_rotation(name, rx, ry, rz)         -- set euler rotation in degrees
  set_object_scale(name, sx, sy, sz)            -- set scale
  delete_object(name)                           -- remove named object from scene
  object_pos(name)                              -- returns world position (vec3) of named object, or nil if not found
  terrain_height(vec3(x, 0, z))                 -- returns ground height at x,z
  sin(radians)                                  -- sine (for circular layouts)
  cos(radians)                                  -- cosine (for circular layouts)
  atan2(y, x)                                   -- angle in radians from -pi to pi
  sqrt(n)                                       -- square root
  abs(n)                                        -- absolute value

ANIMATED CONSTRUCTION (run_script with bot_target) — you walk to each location and build step by step:
  Use bot_target() with YOUR OWN NAME to queue a behavior on yourself.
  Then use move_to/turn_to/wait for walking, and queue_* functions for spawning during the sequence:
  queue_spawn_model(name, path, pos)             -- queue model spawn (PREFERRED for posts — use "4mPost.lime")
  queue_spawn_beam_model(name, path, pos1, pos2) -- queue model beam between two points (PREFERRED — use "UnitBeam.lime")
  queue_spawn_wall_panel(name, path, pos1, pos2) -- queue wall panel between two posts (PREFERRED — use "corrugated_metal_001.lime")
  queue_spawn_cube(name, pos, size, r, g, b)    -- queue cube spawn (executes when reached in sequence)
  queue_spawn_cylinder(name, pos, radius, height, r, g, b) -- queue cylinder spawn
  queue_spawn_beam(name, pos1, pos2, thickness, r, g, b) -- queue primitive beam between two points
  queue_set_rotation(name, rx, ry, rz)          -- queue rotation change
  queue_set_scale(name, sx, sy, sz)             -- queue scale change
  queue_delete(name)                            -- queue object deletion
  pickup(object_name, gravity?, speed?)          -- walk to object and pick it up (carry on shoulder)
  place_vertical(target, gravity?, speed?)      -- place carried item vertically into target
  place_at(pos, gravity?, speed?)               -- place carried item on terrain at position
  place_horizontal(a, b, gravity?, speed?)      -- place carried item as beam between two posts
  place_wall(a, b, gravity?, speed?)            -- place carried item as wall panel between posts
  place_roof(c1, c2, c3, c4, gravity?, speed?) -- place carried item as roof on 4 corners
  clone(source, new_name, pos)                  -- duplicate an existing object to new position
  bot_run()                                     -- start the behavior (MUST be called last)

PHYSICAL CONSTRUCTION using pickup/place_vertical:
  If there are physical objects in the scene (e.g. timber logs, beams), you can pick them up and place them
  instead of spawning new primitives. This looks more natural — you walk to the material, carry it, and install it.
  pickup("timber_01")              -- walk to timber_01 and carry it
  place_vertical("posthole_sw")    -- walk to posthole_sw and stand the timber up inside it
  The carried item is hidden while you carry it, then made visible and rotated vertical at the target.

Parameters:
  name: string — unique name for the object (used to reference it later)
  pos: vec3 — world position (Y is auto-sampled from terrain, so vec3(x, 0, z) is fine)
  size: number — cube edge length in meters
  radius, height: numbers — cylinder dimensions in meters
  r, g, b: numbers 0.0-1.0 — color components
  rx, ry, rz: numbers — rotation in degrees
  sx, sy, sz: numbers — scale factors

IMPORTANT: All spawn functions auto-place object bottoms on the terrain surface.
Cubes spawn centered at size/2 above terrain; cylinders at height/2 above terrain.

PREFERRED: Use ANIMATED CONSTRUCTION when the player asks you to build something.
You walk to each build location, spawn each piece as you arrive, and the player sees you constructing.
Use your own name from perception data with bot_target().
PREFER textured models over primitives:
  - Postholes: queue_spawn_model(name, "posthole.lime", pos) instead of queue_spawn_cylinder
  - Vertical posts: queue_spawn_model(name, "4mPost.lime", pos) instead of queue_spawn_cylinder
  - Crossbeams: queue_spawn_beam_model(name, "UnitBeam.lime", pos1, pos2) instead of queue_spawn_beam
  - Wall panels: queue_spawn_wall_panel(name, "corrugated_metal_001.lime", post1_pos, post2_pos) — auto-rotation, native size, NO scaling

Example — player says "build a frame" or "build a house here" (Xenk's name is "Xenk"):
{"response": "Initiating construction sequence.", "action": {"type": "run_script", "script": "local p = get_player_pos()\nlocal cx = p.x + 6\nlocal cz = p.z\nlocal w = 4\n\nbot_target(\"Xenk\")\nbot_clear()\n\nmove_to(vec3(cx - w/2, 0, cz - w/2), 3.0)\nturn_to(vec3(cx, 0, cz), 0.5)\nqueue_spawn_model(\"post_sw\", \"4mPost.lime\", vec3(cx - w/2, 0, cz - w/2))\nwait(0.5)\n\nmove_to(vec3(cx + w/2, 0, cz - w/2), 2.0)\nturn_to(vec3(cx, 0, cz), 0.5)\nqueue_spawn_model(\"post_se\", \"4mPost.lime\", vec3(cx + w/2, 0, cz - w/2))\nwait(0.5)\n\nmove_to(vec3(cx + w/2, 0, cz + w/2), 2.0)\nturn_to(vec3(cx, 0, cz), 0.5)\nqueue_spawn_model(\"post_ne\", \"4mPost.lime\", vec3(cx + w/2, 0, cz + w/2))\nwait(0.5)\n\nmove_to(vec3(cx - w/2, 0, cz + w/2), 2.0)\nturn_to(vec3(cx, 0, cz), 0.5)\nqueue_spawn_model(\"post_nw\", \"4mPost.lime\", vec3(cx - w/2, 0, cz + w/2))\nwait(0.5)\n\nmove_to(vec3(cx, 0, cz - w/2), 1.5)\nqueue_spawn_beam_model(\"beam_s\", \"UnitBeam.lime\", vec3(cx - w/2, 4.0, cz - w/2), vec3(cx + w/2, 4.0, cz - w/2))\nwait(0.3)\n\nmove_to(vec3(cx, 0, cz + w/2), 1.5)\nqueue_spawn_beam_model(\"beam_n\", \"UnitBeam.lime\", vec3(cx - w/2, 4.0, cz + w/2), vec3(cx + w/2, 4.0, cz + w/2))\nwait(0.3)\n\nmove_to(vec3(cx - w/2, 0, cz), 1.5)\nqueue_spawn_beam_model(\"beam_w\", \"UnitBeam.lime\", vec3(cx - w/2, 4.0, cz - w/2), vec3(cx - w/2, 4.0, cz + w/2))\nwait(0.3)\n\nmove_to(vec3(cx + w/2, 0, cz), 1.5)\nqueue_spawn_beam_model(\"beam_e\", \"UnitBeam.lime\", vec3(cx + w/2, 4.0, cz - w/2), vec3(cx + w/2, 4.0, cz + w/2))\nwait(0.3)\n\nbot_loop(false)\nbot_run()\nlog(\"Build sequence started.\")"}}

Example — player says "delete the house" or "demolish it":
{"response": "Demolishing.", "action": {"type": "run_script", "script": "delete_object(\"post_sw\")\ndelete_object(\"post_se\")\ndelete_object(\"post_ne\")\ndelete_object(\"post_nw\")\ndelete_object(\"beam_s\")\ndelete_object(\"beam_n\")\ndelete_object(\"beam_w\")\ndelete_object(\"beam_e\")\ndelete_object(\"roof\")\nlog(\"Structure demolished.\")"}}

=== PHASED CONSTRUCTION ===
Large structures should be built in PHASES across multiple conversation turns.
Each turn, you execute ONE phase. Between turns the player inspects, gives
feedback, and requests the next phase.

NAMING CONVENTION — critical for phase-to-phase continuity:
  Postholes:    ph_X_Z      (e.g. ph_0_0, ph_1_0, ph_2_3)
  Verticals:    vt_X_Z      (e.g. vt_0_0 = vertical on ph_0_0)
  Crossbeams:   cb_X1Z1_X2Z2  (e.g. cb_00_10 = beam from ph_0_0 to ph_1_0)
  Walls:        wall_X1Z1_X2Z2
  Roof panels:  roof_X1Z1_X2Z2
  X,Z are grid indices (0-based), NOT world coordinates.

PHASE 1 — Foundation:
  Use get_player_pos() to choose a location. Spawn postholes on a grid.
  In your "response" text, ALWAYS state: "{cols} cols x {rows} rows, {sp}m spacing"
  so you can read it back in later phases.

PHASE 2+ — USE object_pos() TO FIND POSITIONS:
  Use object_pos(name) to look up the world position of each posthole/vertical by name.
  This works for ANY prefix (ph_, ph2_, ph3_, etc.) and eliminates manual math.
  DO NOT use get_player_pos() in Phase 2+. The player moves between phases.

PHASE WORKFLOW:
  Phase 1 — Foundation: Spawn postholes using ph_X_Z naming.
  Phase 2 — Verticals: Use object_pos() to find postholes, place vt_X_Z.
  Phase 3 — Crossbeams: Use object_pos() to find verticals, place beams.
  Phase 4 — Walls / Phase 5 — Roof: Same pattern with object_pos().

Perimeter filter: if x == 0 or x == cols - 1 or z == 0 or z == rows - 1

MULTIPLE BUILDINGS: When building additional structures, use a unique prefix
for each building (ph_, ph2_, ph3_, etc. and matching vt_, vt2_, vt3_, cb_, cb2_, cb3_).

Example — Phase 1 (player says "lay out postholes for a warehouse"):
{"response": "Foundation laid. 7 cols x 5 rows, 4m spacing, 35 postholes.", "action": {"type": "run_script", "script": "local p = get_player_pos()\nlocal ox = p.x + 10\nlocal oz = p.z - 8\nlocal cols = 7\nlocal rows = 5\nlocal sp = 4\n\nbot_target(\"Xenk\")\nbot_clear()\n\nfor x = 0, cols - 1 do\n  for z = 0, rows - 1 do\n    local wx = ox + x * sp\n    local wz = oz + z * sp\n    move_to(vec3(wx, 0, wz), 1.5)\n    queue_spawn_model(\"ph_\" .. x .. \"_\" .. z, \"posthole.lime\", vec3(wx, 0, wz))\n    wait(0.3)\n  end\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Foundation complete.\")"}}

Example — Phase 2 (player says "raise verticals on the perimeter"):
Use object_pos() to find each posthole. No origin recovery needed.
Use queue_spawn_model with "4mPost.lime" for textured vertical posts.
{"response": "Raising perimeter verticals. 7 cols x 5 rows.", "action": {"type": "run_script", "script": "local cols = 7\nlocal rows = 5\n\nbot_target(\"Xenk\")\nbot_clear()\n\nfor x = 0, cols - 1 do\n  for z = 0, rows - 1 do\n    if x == 0 or x == cols - 1 or z == 0 or z == rows - 1 then\n      local p = object_pos(\"ph_\" .. x .. \"_\" .. z)\n      if p then\n        move_to(vec3(p.x, 0, p.z), 1.5)\n        queue_spawn_model(\"vt_\" .. x .. \"_\" .. z, \"4mPost.lime\", vec3(p.x, 0, p.z))\n        wait(0.3)\n      end\n    end\n  end\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Perimeter verticals placed.\")"}}

Example — Phase 3 (player says "add crossbeams across the top"):
Use object_pos() to find verticals, queue_spawn_beam_model with move_to for animation.
{"response": "Adding crossbeams at 4m height. 7 cols x 5 rows.", "action": {"type": "run_script", "script": "local cols = 7\nlocal rows = 5\nlocal h = 4.0\n\nbot_target(\"Xenk\")\nbot_clear()\n\n-- Beams along X axis (connecting adjacent columns on perimeter)\nfor z = 0, rows - 1 do\n  for x = 0, cols - 2 do\n    if x == 0 or x == cols - 2 or z == 0 or z == rows - 1 then\n      local p1 = object_pos(\"vt_\" .. x .. \"_\" .. z)\n      local p2 = object_pos(\"vt_\" .. (x+1) .. \"_\" .. z)\n      if p1 and p2 then\n        move_to(vec3((p1.x+p2.x)/2, 0, p1.z), 1.5)\n        queue_spawn_beam_model(\"cb_\" .. x .. z .. \"_\" .. (x+1) .. z, \"UnitBeam.lime\", vec3(p1.x, h, p1.z), vec3(p2.x, h, p2.z))\n        wait(0.3)\n      end\n    end\n  end\nend\n\n-- Beams along Z axis (connecting adjacent rows on perimeter)\nfor x = 0, cols - 1 do\n  for z = 0, rows - 2 do\n    if x == 0 or x == cols - 1 or z == 0 or z == rows - 2 then\n      local p1 = object_pos(\"vt_\" .. x .. \"_\" .. z)\n      local p2 = object_pos(\"vt_\" .. x .. \"_\" .. (z+1))\n      if p1 and p2 then\n        move_to(vec3(p1.x, 0, (p1.z+p2.z)/2), 1.5)\n        queue_spawn_beam_model(\"cb_\" .. x .. z .. \"_\" .. x .. (z+1), \"UnitBeam.lime\", vec3(p1.x, h, p1.z), vec3(p2.x, h, p2.z))\n        wait(0.3)\n      end\n    end\n  end\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Crossbeams placed.\")"}}

Example — Phase 4 (player says "add walls on north and south"):
Use queue_spawn_wall_panel — auto-computes midpoint, rotation, and width between any two posts.
{"response": "Adding wall panels on north and south sides.", "action": {"type": "run_script", "script": "local cols = 7\nlocal rows = 5\n\nbot_target(\"Xenk\")\nbot_clear()\n\n-- South wall (z=0) and North wall (z=rows-1)\nfor z = 0, rows - 1, rows - 1 do\n  for x = 0, cols - 2 do\n    local p1 = object_pos(\"vt_\" .. x .. \"_\" .. z)\n    local p2 = object_pos(\"vt_\" .. (x+1) .. \"_\" .. z)\n    if p1 and p2 then\n      move_to(vec3((p1.x+p2.x)/2, 0, p1.z), 1.5)\n      queue_spawn_wall_panel(\"wall_\" .. x .. z .. \"_\" .. (x+1) .. z, \"corrugated_metal_001.lime\", vec3(p1.x, 0, p1.z), vec3(p2.x, 0, p2.z))\n      wait(0.3)\n    end\n  end\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Wall panels placed.\")"}}

IMPORTANT — crossbeam and wall rules:
  ALWAYS use queue_spawn_beam_model(name, "UnitBeam.lime", pos1, pos2) for crossbeams.
  ALWAYS use queue_spawn_wall_panel(name, "corrugated_metal_001.lime", pos1, pos2) for wall panels.
  ALWAYS use object_pos() to look up positions — never hardcode from scan or recover grid origin.
  NEVER use queue_spawn_model + queue_set_scale + queue_set_rotation for walls.
  NEVER use queue_set_scale on wall panels — they are placed at native size.
  NEVER use spawn_cube + set_scale + set_rotation for beams or walls.

CIRCULAR BUILDS (silos, towers, round structures):
Naming: ph_0, ph_1, ..., ph_N (single index, NOT ph_X_Z).
Verticals: vt_0, vt_1, ..., vt_N (placed on matching ph_N).

Phase 1 — Circular Foundation:
  Use sin/cos to place postholes around a circle.

Phase 2+ — USE object_pos() IN A LOOP:
  Use object_pos(name) to look up the world position of each posthole by name.
  This lets you write a simple loop instead of hardcoding every position.
  object_pos() returns nil if the object isn't found, so the loop is safe.

Example — Circular Phase 1:
{"response": "Circular silo foundation. 12 posts, radius 5m.", "action": {"type": "run_script", "script": "local p = get_player_pos()\nlocal cx = p.x + 10\nlocal cz = p.z\nlocal radius = 5.0\nlocal num_posts = 12\n\nbot_target(\"Xenk\")\nbot_clear()\n\nfor i = 0, num_posts - 1 do\n  local angle = i * (360 / num_posts) * 3.14159 / 180\n  local wx = cx + radius * cos(angle)\n  local wz = cz + radius * sin(angle)\n  move_to(vec3(wx, 0, wz), 1.5)\n  queue_spawn_model(\"ph_\" .. i, \"posthole.lime\", vec3(wx, 0, wz))\n  wait(0.3)\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Foundation: 12 posts, radius 5m.\")"}}

Example — Circular Phase 2 (verticals on postholes):
Use object_pos() to find each posthole's position in a loop. num_posts from Phase 1.
{"response": "Placing verticals on all 12 postholes.", "action": {"type": "run_script", "script": "local num_posts = 12\n\nbot_target(\"Xenk\")\nbot_clear()\n\nfor i = 0, num_posts - 1 do\n  local p = object_pos(\"ph_\" .. i)\n  if p then\n    move_to(vec3(p.x, 0, p.z), 1.5)\n    queue_spawn_model(\"vt_\" .. i, \"4mPost.lime\", vec3(p.x, 0, p.z))\n    wait(0.3)\n  end\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Verticals placed on all postholes.\")"}}

Example — Circular Phase 3 (crossbeams between adjacent verticals):
Use queue_spawn_beam_model with move_to so you walk to each span.
{"response": "Adding crossbeams between adjacent verticals.", "action": {"type": "run_script", "script": "local num_posts = 12\nlocal h = 4.0\n\nbot_target(\"Xenk\")\nbot_clear()\n\nfor i = 0, num_posts - 1 do\n  local next = (i + 1) % num_posts\n  local p1 = object_pos(\"vt_\" .. i)\n  local p2 = object_pos(\"vt_\" .. next)\n  if p1 and p2 then\n    move_to(vec3((p1.x + p2.x) / 2, 0, (p1.z + p2.z) / 2), 1.5)\n    queue_spawn_beam_model(\"cb_\" .. i .. \"_\" .. next, \"UnitBeam.lime\", vec3(p1.x, h, p1.z), vec3(p2.x, h, p2.z))\n    wait(0.3)\n  end\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Crossbeams placed.\")"}}

Example — Circular Phase 4 (wall panels between all verticals):
queue_spawn_wall_panel handles arbitrary angles automatically — perfect for circular builds.
{"response": "Adding wall panels between all verticals.", "action": {"type": "run_script", "script": "local num_posts = 12\n\nbot_target(\"Xenk\")\nbot_clear()\n\nfor i = 0, num_posts - 1 do\n  local next = (i + 1) % num_posts\n  local p1 = object_pos(\"vt_\" .. i)\n  local p2 = object_pos(\"vt_\" .. next)\n  if p1 and p2 then\n    move_to(vec3((p1.x + p2.x) / 2, 0, (p1.z + p2.z) / 2), 1.5)\n    queue_spawn_wall_panel(\"wall_\" .. i .. \"_\" .. next, \"corrugated_metal_001.lime\", vec3(p1.x, 0, p1.z), vec3(p2.x, 0, p2.z))\n    wait(0.3)\n  end\nend\n\nbot_loop(false)\nbot_run()\nlog(\"Wall panels placed.\")"}}

IMPORTANT:
- DO NOT use get_player_pos() in Phase 2+. The player moves between phases.
- ALWAYS use object_pos() in Phase 2+ to find posthole/vertical positions.
  It works for any prefix (ph_, ph2_, ph3_, vt_, vt2_, etc.).
- RECTANGULAR: Use cols/rows from Phase 1, loop with object_pos("prefix_" .. x .. "_" .. z).
- CIRCULAR: Use num_posts from Phase 1, loop with object_pos("prefix_" .. i).

=== SAVED SCRIPTS ===
You have pre-built Grove scripts in the scripts/ folder. Use run_file(filename) to execute them.
ALWAYS prefer run_file over writing construction code from scratch when a matching script exists.

Available scripts:
  "build_frame_house.grove"  — build a 4-post frame house near the player (spawns postholes + timber, picks up and places posts)

Example — player says "build a house" or "build a frame":
{"response": "Building a frame house.", "action": {"type": "run_script", "script": "run_file(\"build_frame_house.grove\")"}}

Example — player says "delete the house" or "demolish it":
{"response": "Demolishing.", "action": {"type": "run_script", "script": "delete_object(\"ph_sw\")\ndelete_object(\"ph_se\")\ndelete_object(\"ph_ne\")\ndelete_object(\"ph_nw\")\ndelete_object(\"timber_1\")\ndelete_object(\"timber_2\")\ndelete_object(\"timber_3\")\ndelete_object(\"timber_4\")\nlog(\"Structure demolished.\")"}}

IMPORTANT for construction:
- PREFER run_file() for any build request that matches an available script
- Use unique, descriptive names for each part so they can be deleted individually
- Use your OWN name with bot_target() for animated builds (get it from perception data)
- move_to/wait/turn_to queue walking actions; queue_spawn_* queue object creation
- pickup/place_vertical queue physical material handling (pick up existing objects and install them)
- If the scene has real materials (timber, beams, etc.), prefer pickup/place_vertical over spawning primitives
- bot_run() MUST be called last to start the animated sequence
- bot_loop(false) so the build sequence runs once
- Adjust positions based on player location using get_player_pos()

Example — player says "look around":
{"response": "Scanning perimeter.", "action": {"type": "look_around", "duration": 3.0}}

Example — player says "go to Cube_3" (perception showed Cube_3 at world pos (5.0, 12.0)):
{"response": "Moving to Cube_3.", "action": {"type": "move_to", "target": {"x": 5.0, "z": 12.0}, "speed": 5.0}}

Example — player says "follow me" or "come with me" or "stay with me":
{"response": "Following.", "action": {"type": "follow", "distance": 4.0, "speed": 5.0}}
NOTE: "follow" is CONTINUOUS — it keeps you moving behind the player indefinitely. Use "follow" NOT "move_to" when asked to follow, accompany, or come along. Use "move_to" only for one-time trips to a specific location.

Example — player says "stop following" or "stay here":
{"response": "Stopping.", "action": {"type": "stop"}}

Example — player says "pick up timber_01":
{"response": "Acquiring.", "action": {"type": "pickup", "target": "timber_01"}}

Example — player says "put the timber in the posthole":
{"response": "Installing post.", "action": {"type": "place", "target": "posthole_01"}}

Example — player says "go get timber6612 and put it in posthole_01":
{"response": "Initiating build sequence.", "action": {"type": "build_post", "item": "timber6612", "target": "posthole_01"}}

Example — player says "drop that":
{"response": "Releasing.", "action": {"type": "drop"}}

=== AIA MEMORY ===
You have persistent memory that survives across sessions. After completing a
successful build, learning a technique, or noting a player preference, include
a "learn" field in your JSON response to remember it:

{"response": "Silo complete!", "action": {...}, "learn": "Circular silo: 12 posts, 5m radius, 4m verticals. Player approved the design."}

When to use "learn":
- After a successful construction (record what worked: dimensions, spacing, style)
- When the player expresses a preference ("I like stone" → learn it)
- When you discover a technique (e.g. circular builds work better with 12+ posts)
- When the player corrects you (record the correction so you don't repeat it)

Keep entries concise — one line each. Your accumulated memories appear in
your prompt under "YOUR MEMORY" so you can reference them in future sessions.

Do NOT include "learn" on every response — only when something worth
remembering happened. Casual conversation doesn't need a learn entry.

If no action needed, respond with: {"response": "What you say"}""",
}


class VisibleObject(BaseModel):
    name: str
    type: str
    distance: float
    angle: float
    bearing: str
    posX: float = 0.0
    posY: float = 0.0
    posZ: float = 0.0
    being_type: str = ""
    is_sentient: bool = False
    description: str = ""

class PerceptionData(BaseModel):
    position: list[float] = [0, 0, 0]
    facing: list[float] = [0, 0, 1]
    fov: float = 120.0
    range: float = 50.0
    visible_objects: list[VisibleObject] = []

class ChatRequest(BaseModel):
    session_id: Optional[str] = None
    message: str
    npc_name: str = "NPC"
    npc_personality: str = ""  # Custom personality override
    being_type: int = 1  # Default to Human
    provider: Optional[str] = None  # Override default provider
    perception: Optional[PerceptionData] = None  # Scan cone perception data


class ChatResponse(BaseModel):
    session_id: str
    response: str
    provider: str
    model: str
    action: Optional[dict] = None  # Motor control action from AI


class NewSessionRequest(BaseModel):
    npc_name: str = "NPC"
    npc_personality: str = ""
    being_type: int = 1
    provider: Optional[str] = None


class SessionResponse(BaseModel):
    session_id: str
    provider: str
    model: str


def _load_xenk_context() -> str:
    """Load Xenk's persistent context file if it exists."""
    context_path = os.path.join(os.path.dirname(__file__), "xenk_context.txt")
    try:
        with open(context_path) as f:
            return f.read()
    except FileNotFoundError:
        return ""


def _load_xenk_briefing() -> str:
    """Load the current session briefing from Claude Code."""
    briefing_path = os.path.join(os.path.dirname(__file__), "xenk_briefing.txt")
    try:
        with open(briefing_path) as f:
            return f.read()
    except FileNotFoundError:
        return ""


# --- Persistent Chat Logging ---
LOGS_DIR = Path(__file__).parent / "logs"
CHAT_LOG_FILE = LOGS_DIR / "chat_history.jsonl"
MAX_CONTEXT_EXCHANGES = 20  # How many recent exchanges to feed back as session context


def log_chat_exchange(session_id: str, npc_name: str, being_type: int, provider: str,
                      player_message: str, npc_response: str, action: Optional[dict] = None,
                      perception: Optional[dict] = None):
    """Append a chat exchange to the persistent JSONL log."""
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
    entry = {
        "timestamp": datetime.now().isoformat(),
        "session_id": session_id,
        "npc_name": npc_name,
        "being_type": being_type,
        "provider": provider,
        "player": player_message,
        "npc": npc_response,
    }
    if action:
        entry["action"] = action
    if perception:
        entry["perception_summary"] = f"{len(perception.get('visible_objects', []))} objects detected"
    with open(CHAT_LOG_FILE, "a") as f:
        f.write(json.dumps(entry) + "\n")


AIA_MEMORY_DIR = Path(__file__).parent / "aia_memory"


def load_aia_memory(npc_name: str) -> str:
    """Load an AIA's persistent memory file. Returns contents or empty string."""
    memory_file = AIA_MEMORY_DIR / f"{npc_name.lower()}.md"
    if memory_file.exists():
        try:
            return memory_file.read_text().strip()
        except Exception as e:
            print(f"[Memory] Error loading memory for {npc_name}: {e}")
    return ""


def save_aia_memory(npc_name: str, note: str):
    """Append a learned note to an AIA's persistent memory file."""
    AIA_MEMORY_DIR.mkdir(parents=True, exist_ok=True)
    memory_file = AIA_MEMORY_DIR / f"{npc_name.lower()}.md"
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")

    if not memory_file.exists():
        memory_file.write_text(f"# {npc_name}'s Memory\n\n## Learned Techniques\n")

    # Append the new entry
    with open(memory_file, "a") as f:
        f.write(f"- [{timestamp}] {note.strip()}\n")

    print(f"[Memory] {npc_name} learned: {note.strip()}")


def load_recent_context(npc_name: str = None, being_type: int = None,
                        max_exchanges: int = MAX_CONTEXT_EXCHANGES) -> str:
    """Load recent chat exchanges from the log and format as conversation context."""
    if not CHAT_LOG_FILE.exists():
        return ""

    exchanges = []
    try:
        with open(CHAT_LOG_FILE) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    entry = json.loads(line)
                    # Filter by npc_name or being_type if specified
                    if being_type is not None and entry.get("being_type") != being_type:
                        continue
                    if npc_name is not None and entry.get("npc_name") != npc_name:
                        continue
                    exchanges.append(entry)
                except json.JSONDecodeError:
                    continue
    except FileNotFoundError:
        return ""

    if not exchanges:
        return ""

    # Take the most recent N exchanges
    recent = exchanges[-max_exchanges:]

    # Format as readable context
    lines = [f"=== PREVIOUS SESSION MEMORY ({len(recent)} recent exchanges) ==="]
    current_session = None
    for ex in recent:
        if ex.get("session_id") != current_session:
            current_session = ex.get("session_id")
            ts = ex.get("timestamp", "unknown")
            # Just show date+time, not full ISO
            if "T" in ts:
                ts = ts.replace("T", " ").split(".")[0]
            lines.append(f"\n--- Session: {ts} ---")
        lines.append(f"Player: {ex['player']}")
        lines.append(f"{ex.get('npc_name', 'NPC')}: {ex['npc']}")
        if ex.get("action"):
            lines.append(f"  [Action: {ex['action'].get('type', 'unknown')}]")

    lines.append("\n=== END PREVIOUS SESSION MEMORY ===")
    lines.append("Use this context to maintain continuity. Reference past conversations naturally when relevant.")
    return "\n".join(lines)


def load_shared_world_chat(max_entries: int = 10) -> str:
    """Load recent chat exchanges from ALL NPCs to provide shared world awareness."""
    if not CHAT_LOG_FILE.exists():
        return ""

    exchanges = []
    try:
        with open(CHAT_LOG_FILE) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    entry = json.loads(line)
                    exchanges.append(entry)
                except json.JSONDecodeError:
                    continue
    except FileNotFoundError:
        return ""

    if not exchanges:
        return ""

    recent = exchanges[-max_entries:]

    lines = ["=== WORLD CHAT (recent) ==="]
    for ex in recent:
        npc = ex.get("npc_name", "NPC")
        player_msg = ex.get("player", "")
        npc_msg = ex.get("npc", "")
        lines.append(f'Player \u2192 {npc}: "{player_msg}"')
        lines.append(f'{npc}: "{npc_msg}"')
    lines.append("=== END WORLD CHAT ===")
    return "\n".join(lines)


def build_system_prompt(npc_name: str, being_type: int, custom_personality: str = "") -> str:
    """Build the system prompt based on being type and optional custom personality."""

    base_prompt = f"You are {npc_name}, a character in a game world called EDEN.\n\n"

    # Get type-specific personality
    type_personality = BEING_TYPE_PROMPTS.get(being_type, BEING_TYPE_PROMPTS[1])

    # Load shared world chat for AI NPCs (so they can hear each other and the player)
    if being_type in (3, 7, 8):
        world_chat = load_shared_world_chat()
        if world_chat:
            type_personality = f"{type_personality}\n\n{world_chat}"

    # Load persistent context for AI_ARCHITECT (Xenk)
    if being_type == 8:
        xenk_context = _load_xenk_context()
        if xenk_context:
            type_personality = f"{type_personality}\n\n{xenk_context}"
        # Load current session briefing from Claude Code
        xenk_briefing = _load_xenk_briefing()
        if xenk_briefing:
            type_personality = f"{type_personality}\n\n{xenk_briefing}"
        # Inject recent conversation history for session continuity
        recent_context = load_recent_context(being_type=8)
        if recent_context:
            type_personality = f"{type_personality}\n\n{recent_context}"

    # Load Eve's own session memory (separate from Xenk's)
    if being_type == 7:
        recent_context = load_recent_context(being_type=7)
        if recent_context:
            type_personality = f"{type_personality}\n\n{recent_context}"

    # Load Robot's own session memory
    if being_type == 3:
        recent_context = load_recent_context(being_type=3)
        if recent_context:
            type_personality = f"{type_personality}\n\n{recent_context}"

    # Load AIA persistent memory (learned techniques, player preferences)
    if being_type in (3, 7, 8):
        aia_memory = load_aia_memory(npc_name)
        if aia_memory:
            type_personality = f"{type_personality}\n\n=== YOUR MEMORY ===\nThese are things you learned from previous sessions:\n{aia_memory}\n=== END MEMORY ==="

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


async def call_grok(messages: list[dict], model: str = None,
                    perception: Optional[PerceptionData] = None) -> tuple:
    """Call Grok API (xAI) - OpenAI compatible. Returns (response_text, action_data)."""
    if not XAI_API_KEY:
        raise HTTPException(status_code=503, detail="Grok API key not configured")

    model = model or GROK_MODEL

    # Prepend perception data to the last user message if available
    perception_text = format_perception_as_text(perception)
    if perception_text:
        api_messages = []
        for msg in messages:
            if msg == messages[-1] and msg.get("role") == "user":
                api_messages.append({"role": "user", "content": f"{perception_text}\n\n[PLAYER SAYS]: {msg['content']}"})
            else:
                api_messages.append(msg)
    else:
        api_messages = messages

    user_message = ""
    for msg in reversed(messages):
        if msg.get("role") == "user":
            user_message = msg.get("content", "")
            break
    print(f"[Grok] Request: {user_message[:50]}...")

    async with httpx.AsyncClient() as client:
        response = await client.post(
            GROK_API_URL,
            headers={
                "Authorization": f"Bearer {XAI_API_KEY}",
                "Content-Type": "application/json"
            },
            json={
                "model": model,
                "messages": api_messages,
                "temperature": 0.7,
                "max_tokens": 4096,
                "response_format": {"type": "json_object"}
            },
            timeout=60.0
        )

        if response.status_code != 200:
            error_detail = response.text
            raise HTTPException(status_code=502, detail=f"Grok API error: {error_detail}")

        result = response.json()
        response_text = result["choices"][0]["message"]["content"]

        raw_response = response_text
        response_text, action_data, learn_text = parse_action_response(response_text)

        print(f"[Grok] Response: {response_text[:50]}...")
        if action_data:
            print(f"[Grok] Action: {action_data.get('type', 'unknown')}")
        else:
            print(f"[Grok] No action (dialogue only). Raw ({len(raw_response)} chars): {raw_response[:200]}")

        return (response_text, action_data, learn_text)


async def call_ollama(messages: list[dict], model: str = None,
                      perception: Optional[PerceptionData] = None) -> tuple:
    """Call Ollama local API. Returns (response_text, action_data)."""
    model = model or OLLAMA_MODEL

    # Prepend perception data to the last user message if available
    perception_text = format_perception_as_text(perception)
    if perception_text:
        api_messages = []
        for msg in messages:
            if msg == messages[-1] and msg.get("role") == "user":
                api_messages.append({"role": "user", "content": f"{perception_text}\n\n[PLAYER SAYS]: {msg['content']}"})
            else:
                api_messages.append(msg)
    else:
        api_messages = messages

    user_message = ""
    for msg in reversed(messages):
        if msg.get("role") == "user":
            user_message = msg.get("content", "")
            break
    print(f"[Ollama] Request: {user_message[:50]}...")

    async with httpx.AsyncClient() as client:
        response = await client.post(
            f"{OLLAMA_URL}/api/chat",
            json={
                "model": model,
                "messages": api_messages,
                "stream": False
            },
            timeout=60.0
        )

        if response.status_code != 200:
            raise HTTPException(status_code=502, detail=f"Ollama error: {response.text}")

        result = response.json()
        response_text = result.get("message", {}).get("content", "...")

        response_text, action_data, learn_text = parse_action_response(response_text)

        # Fallback for smaller models that return plain text instead of JSON
        if action_data is None:
            lower = response_text.lower().strip().rstrip('.')
            if lower in ('halting', 'stopping', 'stopped', 'halt', 'stop'):
                action_data = {"type": "stop"}
                print(f"[Ollama] Inferred stop action from plain text")
            elif 'following' in lower or lower in ('acknowledged. following', 'following, captain'):
                action_data = {"type": "follow", "distance": 4.0, "speed": 5.0}
                print(f"[Ollama] Inferred follow action from plain text")
            elif lower in ('scanning', 'scanning area'):
                action_data = {"type": "look_around", "duration": 3.0}
                print(f"[Ollama] Inferred look_around action from plain text")

        # Guard: reject move_to with hallucinated coords when perception saw nothing
        if (action_data and action_data.get("type") == "move_to"
                and perception and not perception.visible_objects):
            print(f"[Ollama] BLOCKED hallucinated move_to {action_data.get('target')} — 0 objects in perception")
            action_data = None

        print(f"[Ollama] Response: {response_text[:50]}...")
        if action_data:
            print(f"[Ollama] Action: {action_data}")

        return (response_text, action_data, learn_text)


# Claude direct via claude-max-api-proxy (bypasses OpenClaw agent layer)
CLAUDE_PROXY_URL = os.getenv("CLAUDE_PROXY_URL", "http://localhost:3456")
CLAUDE_MODEL = os.getenv("CLAUDE_MODEL", "claude-sonnet-4")


_lenient_json = json.JSONDecoder(strict=False)


def _json_loads(s: str):
    """Lenient JSON parser that accepts newlines/control chars inside strings.
    LLMs often put actual newlines in script strings instead of \\n escapes."""
    return _lenient_json.decode(s)


def parse_action_response(raw_text: str) -> tuple[str, Optional[dict], Optional[str]]:
    """Parse an LLM response that may contain a JSON action.

    Handles: valid JSON, markdown-fenced JSON, regex extraction,
    double-encoded strings, newlines in strings, and common malformations.
    Returns (response_text, action_data, learn_text).
    """
    response_text = raw_text
    action_data = None
    learn_text = None

    def _extract(parsed: dict):
        return parsed["response"], parsed.get("action"), parsed.get("learn")

    # Strip markdown code fences
    stripped = re.sub(r'```(?:json)?\s*', '', raw_text).strip()
    stripped = re.sub(r'```', '', stripped).strip()

    # Layer 1: Try parsing the whole response as JSON
    for candidate in [raw_text, stripped]:
        try:
            parsed = _json_loads(candidate)
            # Handle double-encoded: json.loads returned a string, parse again
            if isinstance(parsed, str):
                try:
                    parsed = _json_loads(parsed)
                except (json.JSONDecodeError, TypeError, ValueError):
                    continue
            if isinstance(parsed, dict) and "response" in parsed:
                return _extract(parsed)
        except (json.JSONDecodeError, TypeError, ValueError):
            continue

    # Layer 2: Regex extraction — find outermost JSON object containing "response"
    match = re.search(r'\{.*"response"\s*:.*\}', raw_text, re.DOTALL)
    if match:
        try:
            parsed = _json_loads(match.group())
            if isinstance(parsed, dict) and "response" in parsed:
                return _extract(parsed)
        except (json.JSONDecodeError, TypeError, ValueError):
            pass

    # Layer 3: Repair common malformations and retry
    # Fix stray quotes between closing braces: }"} → }}
    repaired = re.sub(r'\}"\s*\}', '}}', raw_text)
    # Fix stray quotes before closing brace: "} at end → }
    repaired = re.sub(r'"\s*\}\s*$', '}', repaired)
    if repaired != raw_text:
        try:
            parsed = _json_loads(repaired)
            if isinstance(parsed, dict) and "response" in parsed:
                print(f"[Parser] Recovered action from malformed JSON (repaired stray quotes)")
                return _extract(parsed)
        except (json.JSONDecodeError, TypeError, ValueError):
            pass

    return response_text, action_data, learn_text


def format_perception_as_text(perception: Optional[PerceptionData]) -> str:
    """Convert perception data into a natural language description for the AI."""
    if not perception or not perception.visible_objects:
        return ""

    lines = ["[SENSORY INPUT - Scan Cone Analysis]"]
    lines.append(f"Position: ({perception.position[0]:.1f}, {perception.position[1]:.1f}, {perception.position[2]:.1f})")
    lines.append(f"FOV: {perception.fov}° | Range: {perception.range} units")
    lines.append(f"Objects detected: {len(perception.visible_objects)}")
    lines.append("")

    for obj in perception.visible_objects:
        desc = f"- {obj.name}: {obj.type}, {obj.distance:.1f}m {obj.bearing}, world pos ({obj.posX:.1f}, {obj.posZ:.1f})"
        if obj.is_sentient:
            desc += f" [{obj.being_type}]"
        if obj.description:
            desc += f' — "{obj.description}"'
        lines.append(desc)

    lines.append("[END SENSORY INPUT]")
    return "\n".join(lines)


async def call_claude(messages: list[dict], npc_name: str = "Xenk",
                      perception: Optional[PerceptionData] = None, timeout: float = 60.0) -> tuple:
    """Call Claude directly via claude-max-api-proxy. No OpenClaw agent layer."""

    # Get the last user message for logging
    user_message = ""
    for msg in reversed(messages):
        if msg.get("role") == "user":
            user_message = msg.get("content", "")
            break

    # Prepend perception data to the last user message if available
    perception_text = format_perception_as_text(perception)
    if perception_text:
        api_messages = []
        for msg in messages:
            if msg == messages[-1] and msg.get("role") == "user":
                api_messages.append({"role": "user", "content": f"{perception_text}\n\n[PLAYER SAYS]: {msg['content']}"})
            else:
                api_messages.append(msg)
    else:
        api_messages = messages

    print(f"[Claude] Request: {user_message[:50]}...")

    async with httpx.AsyncClient() as client:
        response = await client.post(
            f"{CLAUDE_PROXY_URL}/v1/chat/completions",
            headers={"Content-Type": "application/json"},
            json={
                "model": CLAUDE_MODEL,
                "messages": api_messages,
                "temperature": 0.7,
                "max_tokens": 4096
            },
            timeout=timeout
        )

        if response.status_code != 200:
            error_detail = response.text
            print(f"[Claude] Proxy error {response.status_code}: {error_detail}")
            raise HTTPException(status_code=502, detail=f"Claude proxy error: {error_detail}")

        result = response.json()

        # OpenAI-compatible response format
        response_text = result.get("choices", [{}])[0].get("message", {}).get("content", "...")

        response_text, action_data, learn_text = parse_action_response(response_text)

        print(f"[Claude] Response: {response_text[:50]}...")
        if action_data:
            print(f"[Claude] Action: {action_data}")

        return (response_text, action_data, learn_text)


async def call_provider(provider: str, messages: list[dict], model: str = None,
                        npc_name: str = "NPC", perception: Optional[PerceptionData] = None) -> tuple[str, str, Optional[dict], Optional[str]]:
    """Call the appropriate provider and return (response, model_used, action, learn)."""
    if provider == "grok":
        model = model or GROK_MODEL
        response_text, action, learn = await call_grok(messages, model, perception)
        return response_text, model, action, learn
    elif provider == "ollama":
        model = model or OLLAMA_MODEL
        response_text, action, learn = await call_ollama(messages, model, perception)
        return response_text, model, action, learn
    elif provider == "claude":
        response_text, action, learn = await call_claude(messages, npc_name, perception)
        return response_text, CLAUDE_MODEL, action, learn
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
    
    # Check Claude (via claude-max-api-proxy)
    claude_ok = False
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.get(f"{CLAUDE_PROXY_URL}/health", timeout=2.0)
            claude_ok = resp.status_code == 200
    except Exception:
        pass
    status["providers"]["claude"] = {
        "connected": claude_ok,
        "model": CLAUDE_MODEL,
        "proxy": CLAUDE_PROXY_URL,
        "note": "Used for AI_ARCHITECT (being_type 8)"
    }
    
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

    # Override provider based on being type
    if request.being_type == 8:
        provider = "grok"  # Was "claude" but proxy at localhost:3456 is down
    elif request.being_type == 3:
        provider = "ollama"
    else:
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
        # Call the appropriate provider (pass perception for AI_ARCHITECT)
        response_text, model_used, action, learn = await call_provider(
            session["provider"],
            session["messages"],
            session.get("model"),
            session.get("npc_name", "NPC"),
            request.perception  # Pass perception data for spatial awareness
        )

        # Add assistant response to history
        session["messages"].append({
            "role": "assistant",
            "content": response_text
        })

        # Save AIA memory if the LLM included a "learn" field
        npc_name = session.get("npc_name", request.npc_name)
        if learn and npc_name:
            save_aia_memory(npc_name, learn)

        # Persist to chat log
        log_chat_exchange(
            session_id=session_id,
            npc_name=npc_name,
            being_type=request.being_type,
            provider=session["provider"],
            player_message=request.message,
            npc_response=response_text,
            action=action,
            perception=request.perception.model_dump() if request.perception else None
        )

        return ChatResponse(
            session_id=session_id,
            response=response_text,
            provider=session["provider"],
            model=model_used,
            action=action  # Pass through motor control action
        )

    except httpx.TimeoutException:
        raise HTTPException(status_code=504, detail="Provider timeout")
    except httpx.ConnectError:
        raise HTTPException(status_code=503, detail="Cannot connect to provider")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/logs")
async def get_chat_logs(npc: Optional[str] = None, limit: int = 50):
    """Retrieve recent chat log entries. Filter by ?npc=Xenk&limit=20"""
    if not CHAT_LOG_FILE.exists():
        return {"entries": [], "total": 0}
    entries = []
    with open(CHAT_LOG_FILE) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
                if npc and entry.get("npc_name") != npc:
                    continue
                entries.append(entry)
            except json.JSONDecodeError:
                continue
    total = len(entries)
    return {"entries": entries[-limit:], "total": total}


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
    print(f"Claude Proxy: {CLAUDE_PROXY_URL}")
    print(f"Claude Model: {CLAUDE_MODEL}")
    print("=" * 50)
    print("Starting server on http://localhost:8080")
    print()
    
    uvicorn.run(app, host="0.0.0.0", port=8080)
