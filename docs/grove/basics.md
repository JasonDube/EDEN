# Language Basics

Grove is a lightweight scripting language designed for EDEN. If you've used Lua, it will feel familiar.

## Variables

```lua
local x = 10
local name = "post_01"
local pos = vec3(5, 0, 10)
```

Use `local` to declare variables. Grove supports numbers, strings, booleans, `vec3`, arrays, and tables.

## Vec3

The `vec3` type represents 3D positions and directions.

```lua
local p = vec3(10, 0, 20)
local x = p.x    -- 10
local y = p.y    -- 0
local z = p.z    -- 20
```

## Strings

Strings use double quotes. Concatenate with `..`:

```lua
local greeting = "Hello " .. "world"
local msg = "Balance: " .. get_credits() .. " CR"
```

## Conditionals

```lua
if x > 10 then
  log("big")
elseif x > 5 then
  log("medium")
else
  log("small")
end
```

## Loops

```lua
-- While loop
local i = 0
while i < 5 do
  log("count: " .. i)
  i = i + 1
end

-- For loop
for i = 1, 10 do
  log("i = " .. i)
end

-- Repeat-until
local n = 0
repeat
  n = n + 1
until n >= 5
```

`break` exits a loop early. `continue` skips to the next iteration.

## Functions

Grove scripts call host functions provided by EDEN. You cannot define your own functions in Grove (yet). All available functions are listed in the [Function Reference](reference.md).

## Output

Use `log()` to print messages. Output appears in the Grove console and in NPC chat responses.

```lua
log("Build complete.")
log("Height at origin: " .. terrain_height(vec3(0, 0, 0)))
```

## Comments

```lua
-- This is a comment
local x = 10  -- inline comment
```
