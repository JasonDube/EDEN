-- serve.ex  --  the VEUPHORIA website, served by Euphoria itself.
--
-- A tiny HTTP server on std/socket.e.  It assembles retro HTML pages in
-- Euphoria and pushes them down a raw socket -- no framework, no magic.
--
--   eui serve.ex            then open  http://localhost:8080   (Ctrl-C stops)

include std/socket.e as sock
include std/text.e            -- trim
include std/sequence.e        -- split

constant BIND_ADDR = "0.0.0.0:8090"   -- 8080 is taken by the AI backend

-- =================  retro HTML  ==========================================

function layout(sequence title, sequence body)
	return
	"<html><head><title>" & title & " -- VEUPHORIA</title></head>\n" &
	"<body bgcolor=\"#c0c0c0\" text=\"#000000\" link=\"#0000cc\" vlink=\"#663399\">\n" &
	"<center>\n" &
	"<table width=\"620\" cellpadding=\"12\"><tr><td>\n" &
	"<center><h1>V&nbsp;E&nbsp;U&nbsp;P&nbsp;H&nbsp;O&nbsp;R&nbsp;I&nbsp;A</h1>\n" &
	"<i>a little shrine to the Euphoria programming language</i></center>\n" &
	"<hr>\n" &
	"<center>[ <a href=\"/\">Home</a> ]&nbsp;&nbsp;" &
	"[ <a href=\"/history\">History</a> ]&nbsp;&nbsp;" &
	"[ <a href=\"/engine\">The Engine</a> ]&nbsp;&nbsp;" &
	"[ <a href=\"/editor\">The Editor</a> ]&nbsp;&nbsp;" &
	"[ <a href=\"/games\">Games</a> ]</center>\n" &
	"<hr>\n" &
	body &
	"<hr>\n" &
	"<font size=\"1\">\n" &
	"<i>An unofficial community site &mdash; not affiliated with the OpenEuphoria Group.</i><br>\n" &
	"Official site: <a href=\"https://openeuphoria.org\">openeuphoria.org</a>&nbsp;&middot;&nbsp;" &
	"Euphoria &copy; Rapid Deployment Software &amp; the OpenEuphoria Group.<br>\n" &
	"<i>Best viewed in Netscape Navigator 4 at 800&times;600.</i>&nbsp;&nbsp;" &
	"This page was assembled and served by Euphoria itself, on std/socket.e.\n" &
	"</font>\n" &
	"</td></tr></table>\n" &
	"</center>\n" &
	"</body></html>\n"
end function

function home()
	return
	"<h2>Welcome, traveller.</h2>\n" &
	"<p>You have reached <b>VEUPHORIA</b> &mdash; a small tribute to <b>Euphoria</b>, " &
	"a programming language from 1993 that hardly anyone remembers, built around one " &
	"beautiful idea: the <b>sequence</b>.</p>\n" &
	"<p>Every page here is served by a web server written <i>in Euphoria</i>. The words " &
	"you are reading were stitched together by Euphoria code and pushed down a raw " &
	"network socket &mdash; no framework, no magic. You could read the whole server in " &
	"a few minutes.</p>\n" &
	"<ul>\n" &
	"<li><a href=\"/history\">The History</a> &mdash; where Euphoria came from</li>\n" &
	"<li><a href=\"/engine\">The Engine</a> &mdash; giving Euphoria pixels again</li>\n" &
	"<li><a href=\"/editor\">The Editor</a> &mdash; diglot, Python and Euphoria side by side</li>\n" &
	"<li><a href=\"/games\">The Games</a> &mdash; Language War &amp; Snake</li>\n" &
	"</ul>\n"
end function

function history()
	return
	"<h2>A Short History</h2>\n" &
	"<p><b>Euphoria</b> was created by <b>Robert Craig</b> of <b>Rapid Deployment " &
	"Software (RDS)</b> and first released around <b>1993</b>. The name is a backronym: " &
	"<i>End User Programming with Hierarchical Objects for Robust Interpreted " &
	"Applications</i> &mdash; but really, it was about making programming feel good.</p>\n" &
	"<p>Its whole world is built from one data type: the <b>sequence</b> &mdash; a list " &
	"that can hold numbers or other sequences, nested to any depth. Strings are simply " &
	"sequences of characters. Sequences are 1-indexed, and you can do arithmetic on a " &
	"whole sequence at once. Simple, and surprisingly powerful.</p>\n" &
	"<p>Euphoria was prized for being <b>fast</b> (it translates to C) and <b>small</b> " &
	"enough to understand completely. In the DOS days it was even used to write games, " &
	"drawing straight to the screen.</p>\n" &
	"<p>In <b>2007</b> RDS released the source, and the <b>OpenEuphoria Group</b> has " &
	"carried it ever since; the current release is <b>4.1.0</b> (2015). It still ships " &
	"with a classic space game, <a href=\"/games\">Language War</a>.</p>\n" &
	"<p><i>With gratitude to RDS, Robert Craig, and everyone in the OpenEuphoria " &
	"community who kept this little language alive.</i></p>\n"
end function

function engine()
	return
	"<h2>The Engine &mdash; VEUPHORIA</h2>\n" &
	"<p>Euphoria lost its graphics when the world moved on from DOS. The old " &
	"<tt>draw_line</tt> and <tt>pixel</tt> calls wrote straight to the video hardware, " &
	"which modern operating systems no longer permit.</p>\n" &
	"<p><b>VEUPHORIA</b> gives them back. It is a small game engine: your Euphoria " &
	"program issues simple commands &mdash; <tt>open_window</tt>, <tt>line</tt>, " &
	"<tt>rect</tt>, <tt>text</tt>, <tt>present</tt> &mdash; that cross into a tiny C " &
	"engine (about 140 lines) which talks to the GPU through SDL. The messy part lives " &
	"in C; your game stays pure, readable Euphoria.</p>\n" &
	"<p>The C backend is deliberately swappable &mdash; SDL today, Vulkan tomorrow &mdash; " &
	"and your Euphoria code never has to change a line.</p>\n" &
	"<p>It already has windows, shapes, keyboard input, timing, and a built-in bitmap " &
	"font for text on screen. Enough to build real games &mdash; see " &
	"<a href=\"/games\">the games</a>.</p>\n" &
	"<center><img src=\"/snake.png\" width=\"480\" border=\"2\" " &
	"alt=\"Snake running on the VEUPHORIA engine\"><br>\n" &
	"<font size=\"1\"><i>Snake, running on VEUPHORIA &mdash; and that very picture " &
	"was rendered by the engine itself.</i></font></center>\n"
end function

function editor()
	return
	"<h2>diglot &mdash; the parallel editor</h2>\n" &
	"<p>Before the engine, there was <b>diglot</b>: a split-screen console editor " &
	"grown from Euphoria's own classic editor, <tt>ed</tt>. <b>Python on the left, " &
	"Euphoria on the right</b> &mdash; like parallel Bible translations &mdash; so you " &
	"can learn both languages side by side.</p>\n" &
	"<center><img src=\"/diglot.png\" width=\"580\" border=\"2\" " &
	"alt=\"diglot: Python on the left, Euphoria on the right\"><br>\n" &
	"<font size=\"1\"><i>diglot: the same little program, in Python and in Euphoria.</i></font></center>\n" &
	"<p>Press <b>Ctrl-E</b> to run the pane you are in. Press <b>Ctrl-A</b> and a local " &
	"AI model translates one side into the other's language. It keeps <tt>ed</tt>'s old " &
	"gray soul &mdash; just words on a screen &mdash; and adds a few new tricks.</p>\n"
end function

function games()
	return
	"<h2>Games</h2>\n" &
	"<h3>Language War</h3>\n" &
	"<p>The classic space game that shipped with Euphoria. The galaxy has fallen to the " &
	"evil <b>C empire</b>; as commander of the starship <b>Euphoria</b> you must clear " &
	"the 50 C ships (and the odd BASIC and Java remnant) and spread euphoria across the " &
	"stars. Pure text-mode &mdash; and it still runs today, unchanged.</p>\n" &
	"<h3>Snake</h3>\n" &
	"<p>The first game built on the VEUPHORIA engine &mdash; a complete game of Snake in " &
	"about seventy lines of Euphoria: a growing snake, food, walls, a live score, and a " &
	"GAME OVER screen. Arrow keys to steer, space to play again.</p>\n" &
	"<p><i>(These run on the desktop, not in your browser &mdash; this site is a shrine, " &
	"not an arcade. Yet.)</i></p>\n"
end function

-- =================  routing + HTTP  ======================================

function route(sequence path)
	if    equal(path, "/")        then return {200, layout("Home",        home())}
	elsif equal(path, "/history") then return {200, layout("History",     history())}
	elsif equal(path, "/engine")  then return {200, layout("The Engine",  engine())}
	elsif equal(path, "/editor")  then return {200, layout("The Editor",  editor())}
	elsif equal(path, "/games")   then return {200, layout("Games",       games())}
	else
		return {404, layout("Not Found",
			"<h2>404 &mdash; Lost in space</h2>\n" &
			"<p>There is no page at <tt>" & path & "</tt>. " &
			"<a href=\"/\">Beam back home.</a></p>\n")}
	end if
end function

function response(integer code, sequence html)
	sequence status = "200 OK"
	if code = 404 then status = "404 Not Found" end if
	return "HTTP/1.1 " & status & "\r\n" &
		"Content-Type: text/html; charset=utf-8\r\n" &
		"Content-Length: " & sprintf("%d", length(html)) & "\r\n" &
		"Connection: close\r\n\r\n" & html
end function

function slurp_bytes(sequence fname)
-- read a whole file into a sequence of bytes (-1 if it doesn't exist)
	integer f = open(fname, "rb")
	if f = -1 then
		return -1
	end if
	sequence data = ""
	object c = getc(f)
	while c != -1 do
		data &= c
		c = getc(f)
	end while
	close(f)
	return data
end function

function image_response(sequence bytes)
	return "HTTP/1.1 200 OK\r\n" &
		"Content-Type: image/png\r\n" &
		"Content-Length: " & sprintf("%d", length(bytes)) & "\r\n" &
		"Connection: close\r\n\r\n" & bytes
end function

procedure main()
	sock:socket server = sock:create(sock:AF_INET, sock:SOCK_STREAM, 0)
	-- allow immediate rebinds after a restart (skip the TIME_WAIT wait)
	sock:set_option(server, sock:SOL_SOCKET, sock:SO_REUSEADDR, 1)
	if sock:bind(server, BIND_ADDR) != sock:OK then
		printf(2, "VEUPHORIA: could not bind %s (error %d) -- is the port in use?\n",
			{BIND_ADDR, sock:error_code()})
		abort(1)
	end if
	puts(1, "VEUPHORIA is serving at http://localhost:8090   (Ctrl-C to stop)\n")

	while sock:listen(server, 10) = sock:OK do
		object client = sock:accept(server)
		if sequence(client) then
			sock:socket cs = client[1]
			object req = sock:receive(cs, 0)
			sequence path = "/"
			if sequence(req) and length(trim(req)) > 0 then
				sequence parts = split(trim(req))
				if length(parts) >= 2 then
					path = parts[2]
				end if
			end if
			sequence full
			if match(".png", path) and not match("..", path) then
				-- serve a static image from the site directory
				object bytes = slurp_bytes("." & path)
				if sequence(bytes) then
					full = image_response(bytes)
				else
					full = response(404, layout("Not Found",
						"<h2>404</h2><p>No image at <tt>" & path & "</tt>.</p>\n"))
				end if
			else
				sequence r = route(path)
				full = response(r[1], r[2])
			end if
			sock:send(cs, full, 0)
			sock:close(cs)
		end if
	end while

	sock:close(server)
end procedure

main()
