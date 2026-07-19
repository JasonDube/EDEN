-- snake.ex  --  the first VEUPHORIA game.  Arrow keys to steer, Esc to quit.
--   ./run snake.ex        (or: ./build.sh && eui snake.ex)

include std/math.e       -- floor  (rand & find are built in)
include veuphoria.e

constant CELL = 20, COLS = 32, ROWS = 24
constant W = COLS * CELL, H = ROWS * CELL
constant STEP = 110      -- ms between moves

function new_food(sequence snake)
-- a random empty cell
	sequence f
	while 1 do
		f = {rand(COLS) - 1, rand(ROWS) - 1}
		if not find(f, snake) then
			return f
		end if
	end while
end function

if not open_window(W, H, "VEUPHORIA Snake  --  arrows steer, Esc quits") then
	puts(2, "could not open a window\n")
	abort(1)
end if

-- snake[1] is the head; start length 3, heading right, in the middle
sequence snake = {{floor(COLS/2),   floor(ROWS/2)},
				  {floor(COLS/2)-1, floor(ROWS/2)},
				  {floor(COLS/2)-2, floor(ROWS/2)}}
integer dx = 1, dy = 0
integer score = 0, alive = 1
sequence food = new_food(snake)

while 1 do
	-- input (poll returns one key per frame)
	integer k = poll()
	if k = -1 then
		exit
	elsif k = KEY_UP    and dy != 1  then dx = 0  dy = -1
	elsif k = KEY_DOWN  and dy != -1 then dx = 0  dy = 1
	elsif k = KEY_LEFT  and dx != 1  then dx = -1 dy = 0
	elsif k = KEY_RIGHT and dx != -1 then dx = 1  dy = 0
	end if

	if alive then
		sequence head = snake[1]
		sequence nh = {head[1] + dx, head[2] + dy}
		-- die on a wall, or on your own body (minus the tail, which moves away)
		if nh[1] < 0 or nh[1] >= COLS or nh[2] < 0 or nh[2] >= ROWS then
			alive = 0
		elsif find(nh, snake[1..$-1]) then
			alive = 0
		else
			snake = {nh} & snake             -- grow a new head
			if equal(nh, food) then
				score += 1
				food = new_food(snake)       -- ate: keep the tail
			else
				snake = snake[1..$-1]        -- moved: drop the tail
			end if
		end if
	end if

	-- draw
	clear(15, 15, 25)
	color(230, 60, 60)                        -- food
	rect(food[1]*CELL, food[2]*CELL, CELL-1, CELL-1, 1)
	if alive then
		color(60, 220, 120)                   -- living snake: green
	else
		color(220, 60, 60)                    -- dead snake: red
	end if
	for i = 1 to length(snake) do
		rect(snake[i][1]*CELL, snake[i][2]*CELL, CELL-1, CELL-1, 1)
	end for
	present()

	if alive then
		delay(STEP)
	else
		delay(1400)                           -- hold on the death frame
		exit
	end if
end while

close_window()
printf(1, "Game over.  Score: %d\n", {score})
