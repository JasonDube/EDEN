-- hello.ex  --  the first VEUPHORIA program.
-- Opens a window and draws a rotating fan of lines.  Esc or close to quit.
--   ./run     (or: ./build.sh && eui hello.ex)

include std/math.e       -- cos, sin, floor, PI
include veuphoria.e

constant W = 800, H = 600
constant CX = W / 2, CY = H / 2

if not open_window(W, H, "VEUPHORIA -- hello") then
	puts(2, "could not open a window\n")
	abort(1)
end if

integer frame = 0
while poll() != -1 do
	clear(10, 10, 30)                       -- dark navy background

	-- a fan of lines from the centre, slowly rotating
	for a = 0 to 350 by 20 do
		atom rad = (a + frame) * PI / 180
		integer g = 128 + floor(127 * sin(rad))
		color(0, g, 200)
		line(CX, CY,
			 CX + floor(260 * cos(rad)),
			 CY + floor(260 * sin(rad)))
	end for

	-- a sweeping line, just to prove it's really animating
	color(255, 200, 0)
	line(0, 0, remainder(frame * 5, W), H - 1)

	present()
	frame += 1
end while

close_window()
