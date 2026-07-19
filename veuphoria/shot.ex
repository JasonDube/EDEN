-- shot.ex  --  draw one posed Snake frame and save it (for the website).
--   SDL_VIDEODRIVER=dummy eui shot.ex     ->  snake.bmp
include veuphoria.e

constant CELL = 20, COLS = 32, ROWS = 24, W = COLS*CELL, H = ROWS*CELL

if not open_window(W, H, "shot") then
	abort(1)
end if

clear(15, 15, 25)

-- food
color(230, 60, 60)
rect(24*CELL, 6*CELL, CELL-1, CELL-1, 1)

-- a curvy snake
sequence body = {{9,12},{10,12},{11,12},{12,12},{13,12},{13,11},{13,10},{14,10},{15,10},{16,10}}
color(60, 220, 120)
for i = 1 to length(body) do
	rect(body[i][1]*CELL, body[i][2]*CELL, CELL-1, CELL-1, 1)
end for

-- score HUD
color(235, 235, 235)
text(6, 6, "SCORE 7", 2)

present()
integer ok = save("snake.bmp")
printf(1, "save -> %d\n", {ok})
close_window()
