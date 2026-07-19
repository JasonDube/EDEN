-- snake.ex  --  VEUPHORIA Snake.
-- Arrow keys steer, eat the red food to grow, don't hit the walls or yourself.
-- SPACE plays again after a crash, Esc quits.
--   ./run snake.ex

include std/math.e       -- floor  (rand & find are built in)
include veuphoria.e

constant CELL = 20, COLS = 32, ROWS = 24
constant W = COLS * CELL, H = ROWS * CELL
constant STEP = 110      -- ms between moves

sequence snake, food
integer dx, dy, score, alive

function new_food()
-- a random empty cell
	sequence f
	while 1 do
		f = {rand(COLS) - 1, rand(ROWS) - 1}
		if not find(f, snake) then
			return f
		end if
	end while
end function

procedure reset_game()
	snake = {{floor(COLS/2),   floor(ROWS/2)},
			 {floor(COLS/2)-1, floor(ROWS/2)},
			 {floor(COLS/2)-2, floor(ROWS/2)}}
	dx = 1  dy = 0
	score = 0
	food = new_food()
	alive = 1
end procedure

procedure centered(sequence s, integer y, integer scale)
	text(floor((W - text_width(s, scale)) / 2), y, s, scale)
end procedure

if not open_window(W, H, "VEUPHORIA Snake") then
	puts(2, "could not open a window\n")
	abort(1)
end if

reset_game()

integer running = 1
while running do
	-- input
	integer k = poll()
	if k = -1 then
		running = 0
	elsif alive then
		if    k = KEY_UP    and dy != 1  then dx = 0  dy = -1
		elsif k = KEY_DOWN  and dy != -1 then dx = 0  dy = 1
		elsif k = KEY_LEFT  and dx != 1  then dx = -1 dy = 0
		elsif k = KEY_RIGHT and dx != -1 then dx = 1  dy = 0
		end if
	end if

	-- move
	if running and alive then
		sequence head = snake[1]
		sequence nh = {head[1] + dx, head[2] + dy}
		if nh[1] < 0 or nh[1] >= COLS or nh[2] < 0 or nh[2] >= ROWS then
			alive = 0
		elsif find(nh, snake[1..$-1]) then
			alive = 0
		else
			snake = {nh} & snake
			if equal(nh, food) then
				score += 1
				food = new_food()
			else
				snake = snake[1..$-1]
			end if
		end if
	end if

	-- draw board
	clear(15, 15, 25)
	color(230, 60, 60)                                  -- food
	rect(food[1]*CELL, food[2]*CELL, CELL-1, CELL-1, 1)
	if alive then color(60, 220, 120) else color(220, 60, 60) end if
	for i = 1 to length(snake) do
		rect(snake[i][1]*CELL, snake[i][2]*CELL, CELL-1, CELL-1, 1)
	end for
	color(235, 235, 235)                                -- score
	text(6, 6, sprintf("SCORE %d", {score}), 2)

	if alive then
		present()
		delay(STEP)
	else
		-- game over overlay
		color(255, 90, 90)
		centered("GAME OVER", floor(H/2) - 60, 4)
		color(235, 235, 235)
		centered(sprintf("SCORE %d", {score}), floor(H/2) + 6, 3)
		centered("SPACE = PLAY AGAIN", floor(H/2) + 56, 2)
		present()
		-- wait for a choice
		integer choice = 0
		while choice = 0 do
			integer kk = poll()
			if kk = -1 then
				choice = -1
			elsif kk = KEY_SPACE then
				choice = 1
			end if
			delay(30)
		end while
		if choice = -1 then
			running = 0
		else
			reset_game()
		end if
	end if
end while

close_window()
printf(1, "Thanks for playing.  Final score: %d\n", {score})
