-- serve.ex  --  the VEUPHORIA website, served by Euphoria itself.
--
-- A tiny HTTP server on std/socket.e.  It assembles retro HTML pages in
-- Euphoria and pushes them down a raw socket -- no framework, no magic.
--
--   eui serve.ex            then open  http://localhost:8080   (Ctrl-C stops)

include std/socket.e as sock
include std/text.e            -- trim
include std/sequence.e        -- split
include std/eds.e             -- guestbook storage (Euphoria Database System)
include std/datetime.e as dt  -- timestamps

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
	"[ <a href=\"/games\">Games</a> ]&nbsp;&nbsp;" &
	"[ <a href=\"/guestbook\">Guestbook</a> ]</center>\n" &
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
	"<li><a href=\"/guestbook\">The Guestbook</a> &mdash; sign it (backed by Euphoria's own database)</li>\n" &
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

-- =================  guestbook (EDS)  =====================================

function html_escape(sequence s)
-- make user text safe to drop into a page
	sequence out = ""
	for i = 1 to length(s) do
		integer c = s[i]
		if    c = '&' then out &= "&amp;"
		elsif c = '<' then out &= "&lt;"
		elsif c = '>' then out &= "&gt;"
		elsif c = '"' then out &= "&quot;"
		else out &= c
		end if
	end for
	return out
end function

function hexval(integer c)
	if    c >= '0' and c <= '9' then return c - '0'
	elsif c >= 'a' and c <= 'f' then return c - 'a' + 10
	elsif c >= 'A' and c <= 'F' then return c - 'A' + 10
	end if
	return 0
end function

function url_decode(sequence s)
-- undo form-url-encoding: '+' -> space, %XX -> byte
	sequence out = ""
	integer i = 1
	while i <= length(s) do
		if s[i] = '+' then
			out &= ' '
			i += 1
		elsif s[i] = '%' and i + 2 <= length(s) then
			out &= hexval(s[i+1]) * 16 + hexval(s[i+2])
			i += 3
		else
			out &= s[i]
			i += 1
		end if
	end while
	return out
end function

function form_value(sequence body, sequence key)
-- pull one field out of a  name=..&message=..  form body
	sequence pairs = split(body, '&')
	for i = 1 to length(pairs) do
		integer eq = find('=', pairs[i])
		if eq and equal(pairs[i][1..eq-1], key) then
			return url_decode(pairs[i][eq+1..$])
		end if
	end for
	return ""
end function

procedure open_guestbook()
-- open (or first create) the EDS database that holds the entries
	if db_open("guestbook.edb", DB_LOCK_NO) != DB_OK then
		if db_create("guestbook.edb", DB_LOCK_NO) != DB_OK then
			puts(2, "guestbook: could not create guestbook.edb\n")
			abort(1)
		end if
		if db_create_table("entries") != DB_OK then
			puts(2, "guestbook: could not create table\n")
			abort(1)
		end if
	end if
	if db_select_table("entries") != DB_OK then
		puts(2, "guestbook: could not select table\n")
		abort(1)
	end if
end procedure

procedure add_entry(sequence name, sequence message)
	name = trim(name)
	message = trim(message)
	if length(name) = 0 then name = "anonymous" end if
	if length(name) > 40 then name = name[1..40] end if
	if length(message) > 2000 then message = message[1..2000] end if
	if length(message) = 0 then
		return                       -- ignore empty sign-ins
	end if
	sequence when = dt:format(dt:now(), "%Y-%m-%d %H:%M")
	integer key = db_table_size() + 1
	if db_insert(key, {name, message, when}) != DB_OK then
		-- silently ignore a failed insert; the page will just not show it
	end if
end procedure

function guestbook_page()
	sequence entries = ""
	integer n = db_table_size()
	if n = 0 then
		entries = "<p><i>No entries yet &mdash; be the first to sign!</i></p>\n"
	else
		for rec = n to 1 by -1 do        -- newest first
			object d = db_record_data(rec)
			entries &=
				"<p><b>" & html_escape(d[1]) & "</b> " &
				"<font size=\"1\" color=\"#555555\">(" & html_escape(d[3]) & ")</font><br>\n" &
				html_escape(d[2]) & "</p>\n"
		end for
	end if
	return
	"<h2>Guestbook</h2>\n" &
	"<p>Leave your mark. This page is backed by <b>EDS</b>, Euphoria's own database " &
	"&mdash; your words are written to <tt>guestbook.edb</tt> and will outlast the " &
	"server.</p>\n" &
	"<form action=\"/sign\" method=\"post\">\n" &
	"<table cellpadding=\"3\">\n" &
	"<tr><td align=\"right\">Name:</td>" &
	"<td><input type=\"text\" name=\"name\" size=\"32\" maxlength=\"40\"></td></tr>\n" &
	"<tr><td align=\"right\" valign=\"top\">Message:</td>" &
	"<td><textarea name=\"message\" rows=\"4\" cols=\"44\" wrap=\"virtual\"></textarea></td></tr>\n" &
	"<tr><td></td><td><input type=\"submit\" value=\"Sign the guestbook\"></td></tr>\n" &
	"</table>\n</form>\n" &
	"<hr>\n<h3>Entries (" & sprintf("%d", n) & ")</h3>\n" &
	entries
end function

-- =================  routing + HTTP  ======================================

function route(sequence path)
	if    equal(path, "/")        then return {200, layout("Home",        home())}
	elsif equal(path, "/history") then return {200, layout("History",     history())}
	elsif equal(path, "/engine")  then return {200, layout("The Engine",  engine())}
	elsif equal(path, "/editor")  then return {200, layout("The Editor",  editor())}
	elsif equal(path, "/games")   then return {200, layout("Games",       games())}
	elsif equal(path, "/guestbook") then return {200, layout("Guestbook", guestbook_page())}
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
	open_guestbook()
	puts(1, "VEUPHORIA is serving at http://localhost:8090   (Ctrl-C to stop)\n")

	while sock:listen(server, 10) = sock:OK do
		object client = sock:accept(server)
		if sequence(client) then
			sock:socket cs = client[1]
			object req = sock:receive(cs, 0)
			sequence method = "GET", path = "/", body = ""
			if sequence(req) and length(req) > 0 then
				integer nl = match("\r\n", req)
				sequence firstline = req
				if nl then firstline = req[1..nl-1] end if
				sequence parts = split(trim(firstline))
				if length(parts) >= 1 then method = parts[1] end if
				if length(parts) >= 2 then path  = parts[2] end if
				integer bsep = match("\r\n\r\n", req)
				if bsep then body = req[bsep+4..$] end if
			end if

			sequence full
			if equal(method, "POST") and equal(path, "/sign") then
				-- a guestbook sign-in: save it, then redirect (Post/Redirect/Get)
				add_entry(form_value(body, "name"), form_value(body, "message"))
				full = "HTTP/1.1 303 See Other\r\n" &
					"Location: /guestbook\r\n" &
					"Content-Length: 0\r\nConnection: close\r\n\r\n"
			elsif match(".png", path) and not match("..", path) then
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
