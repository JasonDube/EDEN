HOW TO WRITE A BOT'S PERSONALITY
================================

Each robot is a .glb file (e.g. heretic.glb, qwen_bot_glb.glb, gemma_1.glb).
To give a robot a character, make a text file next to it with the SAME name
but a .persona extension:

    heretic.glb        ->  heretic.persona
    qwen_bot_glb.glb   ->  qwen_bot_glb.persona
    gemma_1.glb        ->  gemma_1.persona

Just write plain text in the file describing WHO the bot is — its role, its
mood, how it talks, what it cares about. Address the bot as "you" (it's the
system prompt). See heretic.persona for a working example.

IT'S LIVE: the file is re-read every time you talk to the bot, so you can edit
the persona and just send another message to see the new character — no need
to rebuild or redeploy. (If you ADD a .persona file for a bot that didn't have
one, press J in the folder once so the game notices the file.)

ALWAYS ADDED AUTOMATICALLY (don't write these yourself):
  - Web search: every bot can look things up on the live web when asked.
  - Gemma's "fun room" guardian mechanic stays in effect while the room is
    locked, no matter what gemma_1.persona says; once she opens it, her file
    (if any) takes over.

If a robot has NO .persona file, it uses its built-in default.
