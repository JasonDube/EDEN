-- veuphoria.e  --  Euphoria binding for the VEUPHORIA engine.
--
-- This is the whole bridge: open the shared library, name each C command, and
-- wrap it in a friendly Euphoria call.  Your game just does:
--
--   include veuphoria.e
--   if open_window(800,600,"my game") then
--       while poll() != -1 do
--           clear(0,0,0)
--           line(0,0, 799,599)
--           present()
--       end while
--       close_window()
--   end if
--
-- The engine (libveuphoria.so) does the SDL/GPU work; you stay in Euphoria.

include std/dll.e        -- open_dll, define_c_func/proc, c_func/proc, C_INT...
include std/machine.e    -- allocate_string, free

atom lib = open_dll("./libveuphoria.so")
if lib = 0 then
	puts(2, "veuphoria: can't open ./libveuphoria.so -- run ./build.sh first\n")
	abort(1)
end if

-- name the C commands (routine ids)
constant
	_open    = define_c_func(lib, "veu_open",    {C_INT, C_INT, C_POINTER}, C_INT),
	_clear   = define_c_proc(lib, "veu_clear",   {C_INT, C_INT, C_INT}),
	_color   = define_c_proc(lib, "veu_color",   {C_INT, C_INT, C_INT}),
	_line    = define_c_proc(lib, "veu_line",    {C_INT, C_INT, C_INT, C_INT}),
	_rect    = define_c_proc(lib, "veu_rect",    {C_INT, C_INT, C_INT, C_INT, C_INT}),
	_present = define_c_proc(lib, "veu_present", {}),
	_poll    = define_c_func(lib, "veu_poll",    {}, C_INT),
	_key     = define_c_func(lib, "veu_key",     {C_INT}, C_INT),
	_delay   = define_c_proc(lib, "veu_delay",   {C_INT}),
	_ticks   = define_c_func(lib, "veu_ticks",   {}, C_INT),
	_close   = define_c_proc(lib, "veu_close",   {})

-- named keys, fetched from the engine so the SDL values are always correct.
-- poll() returns one of these for arrows/space/enter; letter & number keys
-- come back as their ASCII code (so you can compare poll() to 'w', ' ', etc.)
public constant
	KEY_RIGHT = c_func(_key, {0}),
	KEY_LEFT  = c_func(_key, {1}),
	KEY_DOWN  = c_func(_key, {2}),
	KEY_UP    = c_func(_key, {3}),
	KEY_SPACE = c_func(_key, {4}),
	KEY_ENTER = c_func(_key, {5})

-- ---- the friendly Euphoria API ------------------------------------------

public function open_window(integer w, integer h, sequence title)
-- open a window; returns 1 on success, 0 on failure
	atom p = allocate_string(title)
	integer ok = c_func(_open, {w, h, p})
	free(p)
	return ok
end function

public procedure clear(integer r, integer g, integer b)
	c_proc(_clear, {r, g, b})
end procedure

public procedure color(integer r, integer g, integer b)
	c_proc(_color, {r, g, b})
end procedure

public procedure line(integer x1, integer y1, integer x2, integer y2)
	c_proc(_line, {x1, y1, x2, y2})
end procedure

public procedure rect(integer x, integer y, integer w, integer h, integer fill)
	c_proc(_rect, {x, y, w, h, fill})
end procedure

public procedure present()
	c_proc(_present, {})
end procedure

public function poll()
-- pump events: -1 = quit, 0 = nothing, >0 = a keycode (see KEY_* / ASCII)
	return c_func(_poll, {})
end function

public procedure delay(integer ms)
-- pause for ms milliseconds (steady frame pacing)
	c_proc(_delay, {ms})
end procedure

public function ticks()
-- milliseconds since the engine started
	return c_func(_ticks, {})
end function

public procedure close_window()
	c_proc(_close, {})
end procedure
