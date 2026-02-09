# AlgoBot Programming

AlgoBots are worker robots that execute Grove scripts autonomously. You program them by selecting a target bot, queuing actions, and running the behavior.

## Basic Pattern

```lua
bot_target("Worker1")           -- select the bot
bot_clear()                     -- clear previous program
move_to(vec3(10, 0, 20), 3.0)  -- queue actions
wait(1.0)
move_to(vec3(0, 0, 0), 3.0)
bot_loop(true)                  -- repeat forever
bot_run()                       -- activate
```

## Movement

### move_to(position, duration, animation?)

Move the bot to a world position over a duration in seconds.

```lua
move_to(vec3(50, 0, 80), 5.0)              -- walk over 5 seconds
move_to(vec3(50, 0, 80), 3.0, "run")       -- run animation while moving
```

### turn_to(position, duration?)

Rotate the bot to face a world position (yaw only). Default duration is 0.5 seconds.

```lua
turn_to(vec3(100, 0, 0), 1.0)   -- turn to face position over 1 second
```

### follow_path(path_name)

Follow a named AI path created in the editor.

```lua
follow_path("patrol_route_1")
```

## Timing

### wait(seconds)

Pause before the next action.

```lua
wait(2.0)   -- wait 2 seconds
```

## Visibility & Animation

```lua
set_visible(false)              -- hide the bot
set_visible(true)               -- show it again
play_anim("wave", 2.0)         -- play animation for 2 seconds
```

## Signals

Send a named signal that can trigger other entities' behaviors:

```lua
send_signal("alarm")            -- broadcast to all
send_signal("open", "Gate_01")  -- send to specific object
```

## Looping

```lua
bot_loop(true)    -- repeat the program when it finishes
bot_loop(false)   -- run once and stop (default)
```

## Examples

### Patrol Between Two Points

```lua
bot_target("Guard_1")
bot_clear()
move_to(vec3(10, 0, 0), 4.0)
wait(2.0)
turn_to(vec3(-10, 0, 0), 0.5)
move_to(vec3(-10, 0, 0), 4.0)
wait(2.0)
turn_to(vec3(10, 0, 0), 0.5)
bot_loop(true)
bot_run()
```

### Circular Route

```lua
bot_target("Bot_1")
bot_clear()
move_to(vec3(10, 0, 0), 3.0)
move_to(vec3(0, 0, 10), 3.0)
move_to(vec3(-10, 0, 0), 3.0)
move_to(vec3(0, 0, -10), 3.0)
bot_loop(true)
bot_run()
```

### Resource Collector

```lua
bot_target("Hauler_1")
bot_clear()
-- Check if there's wood at the destination
local res = zone_resource(vec3(500, 0, 500))
if res == "wood" then
  move_to(vec3(500, 0, 500), 10.0)
  wait(5.0)
  move_to(vec3(0, 0, 0), 10.0)
  wait(2.0)
  bot_loop(true)
  bot_run()
else
  log("No wood at target location")
end
```

!!! warning
    Always call `bot_clear()` before adding new commands, or actions will append to the previous program. Always call `bot_run()` last.
