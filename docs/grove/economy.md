# Economy & Land

Grove provides functions for managing credits and purchasing land plots.

## Credits

```lua
-- Check balance
local balance = get_credits()
log("Balance: " .. balance .. " CR")

-- Add credits
add_credits(1000)

-- Deduct credits (returns true if sufficient funds)
local ok = deduct_credits(500)
if ok then
  log("Payment processed.")
else
  log("Insufficient funds.")
end
```

## Land Ownership

The world is divided into a grid of plots. Each plot has a zone type and can be purchased.

### Checking Plot Status

Always check before buying:

```lua
local pos = vec3(100, 0, 200)
local status = plot_status(pos)

if status == "available" then
  log("Plot available for " .. plot_price(pos) .. " CR")
elseif status == "owned" then
  log("Already owned.")
elseif status == "spawn_zone" then
  log("Spawn zone — not for sale.")
elseif status == "battlefield" then
  log("Battlefield — not for sale.")
elseif status == "too_expensive" then
  log("Can't afford it. Price: " .. plot_price(pos) .. " CR")
end
```

### Buying and Selling

```lua
-- Buy a plot
local ok = buy_plot(vec3(100, 0, 200))
if ok then
  log("Purchased!")
end

-- Sell a plot (50% refund)
sell_plot(vec3(100, 0, 200))
```

### Zone Queries

```lua
-- What type of zone is this?
local zt = zone_type(vec3(100, 0, 200))
-- Returns: "wilderness", "battlefield", "spawn_safe", "residential",
--          "commercial", "industrial", "resource"

-- What resource is here?
local res = zone_resource(vec3(500, 0, 500))
-- Returns: "wood", "limestone", "iron", "oil", "none"

-- Who owns this plot? (0 = unclaimed)
local owner = zone_owner(vec3(100, 0, 200))

-- Can I build here?
local allowed = can_build(vec3(100, 0, 200))
```

## Example: Buy Land and Build

```lua
local pos = get_player_pos()
local status = plot_status(pos)

if status == "available" then
  local price = plot_price(pos)
  if deduct_credits(price) then
    buy_plot(pos)
    -- Build a marker post
    spawn_cylinder("claim_post", pos, 0.1, 2.0, 1.0, 0.85, 0.0)
    log("Plot purchased and marked. " .. get_credits() .. " CR remaining.")
  end
else
  log("Cannot buy: " .. status)
end
```
