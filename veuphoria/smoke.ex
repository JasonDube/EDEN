-- smoke.ex  --  headless self-test of the VEUPHORIA bridge.
-- Run with the SDL "dummy" video driver so it needs no display:
--   SDL_VIDEODRIVER=dummy eui smoke.ex
-- Exercises the full path: Euphoria -> FFI -> C -> SDL, with no window shown.

include veuphoria.e

printf(1, "keys: UP=%d DOWN=%d LEFT=%d RIGHT=%d SPACE=%d ENTER=%d\n",
	   {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SPACE, KEY_ENTER})
delay(5)
puts(1, "delay() ran\n")

integer ok = open_window(320, 240, "smoke")
printf(1, "open_window -> %d\n", {ok})
if ok then
	clear(0, 0, 0)
	color(255, 255, 255)
	line(0, 0, 319, 239)
	rect(50, 50, 100, 80, 1)
	present()
	close_window()
	puts(1, "clear/color/line/rect/present/close all ran -- bridge OK\n")
else
	puts(1, "(window failed -- but the FFI symbols still resolved)\n")
end if
