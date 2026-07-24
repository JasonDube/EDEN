-- serve.ex  --  the EDEN OS website, served by Euphoria itself.
--
-- A tiny HTTP server on std/socket.e.  It assembles the pages in Euphoria and
-- pushes them down a raw socket -- no framework, no magic.  The same trick the
-- VEUPHORIA shrine uses; here it flies the ship's-terminal colours instead.
--
--   eui serve.ex            then open  http://localhost:8091   (Ctrl-C stops)

include std/socket.e as sock
include std/text.e            -- trim
include std/sequence.e        -- split
include std/eds.e             -- ship's log storage (Euphoria Database System)
include std/datetime.e as dt  -- timestamps

constant BIND_ADDR = "0.0.0.0:8091"   -- 8080 = AI backend, 8090 = VEUPHORIA

-- =================  page shell  =========================================

function layout(sequence title, sequence body)
	return
	"<!doctype html><html><head>\n" &
	"<meta charset=\"utf-8\">\n" &
	"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n" &
	"<title>" & title & " -- EDEN OS</title>\n" &
	"<style>\n" &
	"  :root{--bg:#05070d;--ink:#c8f5d8;--dim:#5f8f78;--hot:#7fe0ff;--warn:#ffd166;--line:#12331f;}\n" &
	"  *{box-sizing:border-box;}\n" &
	"  body{margin:0;background:var(--bg);color:var(--ink);\n" &
	"       font-family:'DejaVu Sans Mono',Consolas,monospace;font-size:15px;line-height:1.6;\n" &
	"       background-image:radial-gradient(circle at 20% 15%,#0d1830 0,transparent 45%),\n" &
	"                        radial-gradient(circle at 82% 78%,#131024 0,transparent 40%);}\n" &
	"  .wrap{max-width:760px;margin:0 auto;padding:26px 20px 60px;}\n" &
	"  header.top{text-align:center;border-bottom:1px solid var(--line);padding-bottom:14px;}\n" &
	"  h1.brand{margin:.2em 0;letter-spacing:.5em;font-size:1.9em;color:var(--ink);\n" &
	"           text-shadow:0 0 14px rgba(127,224,255,.35);}\n" &
	"  .tag{color:var(--dim);font-style:italic;}\n" &
	"  nav{text-align:center;margin:14px 0 4px;}\n" &
	"  nav a{color:var(--hot);text-decoration:none;margin:0 .5em;}\n" &
	"  nav a:hover{text-decoration:underline;}\n" &
	"  a{color:var(--hot);}\n" &
	"  h2{color:var(--hot);border-left:3px solid var(--hot);padding-left:.5em;margin-top:1.5em;}\n" &
	"  h3{color:var(--warn);margin-top:1.3em;}\n" &
	"  b{color:#eafff2;}\n" &
	"  code,kbd{background:#0c1420;border:1px solid var(--line);border-radius:4px;\n" &
	"           padding:1px 6px;color:var(--warn);font-size:.92em;}\n" &
	"  kbd{color:var(--hot);border-color:#1d4a5a;box-shadow:0 1px 0 #061018;}\n" &
	"  table.keys{width:100%;border-collapse:collapse;margin:.6em 0;}\n" &
	"  table.keys td{border-bottom:1px solid var(--line);padding:6px 8px;vertical-align:top;}\n" &
	"  table.keys td.k{white-space:nowrap;width:34%;color:var(--hot);}\n" &
	"  .note{color:var(--dim);font-size:.9em;}\n" &
	"  hr{border:0;border-top:1px solid var(--line);margin:1.4em 0;}\n" &
	"  form input,form textarea{background:#0a1018;color:var(--ink);border:1px solid #1d4a5a;\n" &
	"       border-radius:4px;padding:6px;font-family:inherit;}\n" &
	"  form input[type=submit]{color:var(--hot);cursor:pointer;}\n" &
	"  footer{margin-top:2.4em;color:var(--dim);font-size:.8em;text-align:center;}\n" &
	"</style></head>\n" &
	"<body><div class=\"wrap\">\n" &
	"<header class=\"top\">\n" &
	"<h1 class=\"brand\">E D E N&nbsp;&nbsp;O S</h1>\n" &
	"<div class=\"tag\">a game whose setting is a real operating system</div>\n" &
	"<nav>\n" &
	"[ <a href=\"/\">Bridge</a> ]&nbsp;\n" &
	"[ <a href=\"/play\">How to Play</a> ]&nbsp;\n" &
	"[ <a href=\"/ship\">The Ship</a> ]&nbsp;\n" &
	"[ <a href=\"/bots\">The Crew</a> ]&nbsp;\n" &
	"[ <a href=\"/log\">Ship's Log</a> ]\n" &
	"</nav></header>\n" &
	"<main>\n" & body & "</main>\n" &
	"<footer>\n" &
	"Assembled and served by <b>Euphoria</b> itself, on <code>std/socket.e</code> &mdash; no framework, no magic.<br>\n" &
	"EDEN OS is a work in progress. This log outlasts the server.\n" &
	"</footer>\n" &
	"</div></body></html>\n"
end function

-- =================  content  ============================================

function home()
	return
	"<h2>Welcome to the bridge.</h2>\n" &
	"<p>You are aboard <b>EDEN OS</b> &mdash; a vast, shape-shifting starship that is " &
	"also <b>your mind</b>. A computer has always been an extension of the mind the way a " &
	"hammer extends the arm; here that idea is taken literally. The ship's rooms are your " &
	"folders, its crew are AI minds you can speak to, and the view out the window is the " &
	"rest of the cosmos.</p>\n" &
	"<p>The strange part: it is a <b>real operating system</b>. The rooms are your actual " &
	"files. When the ship's crew tidies a room, real files move. When you seal a door, a " &
	"real folder is guarded. The game and the machine are the same thing.</p>\n" &
	"<ul>\n" &
	"<li><a href=\"/play\">How to Play</a> &mdash; fly the ship, move things, talk to the crew</li>\n" &
	"<li><a href=\"/ship\">The Ship</a> &mdash; what EDEN OS actually is</li>\n" &
	"<li><a href=\"/bots\">The Crew</a> &mdash; the minds that live aboard</li>\n" &
	"<li><a href=\"/log\">Ship's Log</a> &mdash; leave a mark (backed by Euphoria's own database)</li>\n" &
	"</ul>\n" &
	"<p class=\"note\">New here? Start with <a href=\"/play\">How to Play</a>.</p>\n"
end function

function play()
	return
	"<h2>How to Play</h2>\n" &
	"<p>EDEN OS drops you at the <b>helm</b> the moment you enter &mdash; standing at your " &
	"home directory, looking out into space. You move like a spirit in zero effort: fly " &
	"anywhere, in any direction.</p>\n" &

	"<h3>Flying the ship</h3>\n" &
	"<table class=\"keys\">\n" &
	"<tr><td class=\"k\"><kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd></td>" &
	"<td>Fly forward / left / back / right, relative to where you are looking.</td></tr>\n" &
	"<tr><td class=\"k\"><b>Mouse</b></td><td>Look around.</td></tr>\n" &
	"<tr><td class=\"k\"><kbd>Space</kbd></td><td>Rise straight up.</td></tr>\n" &
	"<tr><td class=\"k\"><kbd>Shift</kbd></td><td>Sink straight down.</td></tr>\n" &
	"<tr><td class=\"k\"><kbd>Ctrl</kbd> (held)</td><td>Boost &mdash; fly three times faster.</td></tr>\n" &
	"<tr><td class=\"k\"><kbd>Alt</kbd></td><td>Free the mouse cursor to click panels and type; " &
	"press again to recapture it for looking around.</td></tr>\n" &
	"</table>\n" &

	"<h3>Rooms, frames and mounts</h3>\n" &
	"<p>Every folder is a <b>silo</b> &mdash; a round room whose walls are lined with " &
	"<b>frames</b> (the panels). On each frame sits a <b>mount</b>: a sub-folder shown as a " &
	"door, or a file shown as itself &mdash; a picture, a video, a 3D model.</p>\n" &

	"<h3>Getting around</h3>\n" &
	"<table class=\"keys\">\n" &
	"<tr><td class=\"k\"><b>Right-click</b> a directory</td>" &
	"<td>Instantly transport into that folder &mdash; no walking. The ship dissolves and " &
	"rebuilds the new room around you from the real folder on disk. This is the fast way to " &
	"travel the whole tree.</td></tr>\n" &
	"<tr><td class=\"k\">Fly through a door</td>" &
	"<td>The scenic way in &mdash; same destination, but you drift through it.</td></tr>\n" &
	"<tr><td class=\"k\"><kbd>Home</kbd></td>" &
	"<td>Recentre: drops you back on the floor at the middle of the silo you are in. Use it " &
	"any time you get lost out among the frames.</td></tr>\n" &
	"</table>\n" &

	"<h3>Copy &amp; paste &mdash; moving mounts</h3>\n" &
	"<p>You rearrange the ship by carrying mounts from one silo to another, using a " &
	"<b>hotbar</b> of ten slots along the bottom of the screen. It works just like cut and " &
	"paste &mdash; except it is a real cut, so the file truly relocates.</p>\n" &
	"<table class=\"keys\">\n" &
	"<tr><td class=\"k\">1. <b>Copy:</b> aim at a mount, press <kbd>1</kbd>&ndash;<kbd>0</kbd></td>" &
	"<td>Lifts it off the wall and into that hotbar slot. It leaves the frame right away.</td></tr>\n" &
	"<tr><td class=\"k\">2. <b>Travel:</b> right-click a door</td>" &
	"<td>Carry your slot to any other silo in the tree.</td></tr>\n" &
	"<tr><td class=\"k\">3. <b>Paste:</b> aim at an empty frame, press its <kbd>number</kbd></td>" &
	"<td>Drops the mount there. The real file <b>moves</b> &mdash; it leaves the old folder " &
	"and arrives in the new one, and stays put when you come back.</td></tr>\n" &
	"</table>\n" &
	"<p class=\"note\">Because it is a move, a mount lives in exactly one place at a time &mdash; " &
	"grab it, fly, paste, done.</p>\n" &

	"<h3>Throwing things</h3>\n" &
	"<table class=\"keys\">\n" &
	"<tr><td class=\"k\"><kbd>Q</kbd></td>" &
	"<td>Throw the held item out into the world as a physics object. A 3D model flies as " &
	"itself; anything else flies as a labelled cube.</td></tr>\n" &
	"</table>\n" &

	"<h3>Talking to the crew</h3>\n" &
	"<p><b>Click a crew member</b> and a small terminal opens at the bottom-left of the " &
	"screen. Type a line, press <kbd>Enter</kbd>, and it closes so you can move again; their " &
	"reply appears as a <b>speech bubble</b> over their head. Click them again to say more. " &
	"The crew are real AI minds &mdash; see <a href=\"/bots\">The Crew</a>.</p>\n" &
	"<p class=\"note\">A crew member only answers when their server is running. The ship's " &
	"<b>server racks</b> are physical models you can fly to and switch on; a green light means " &
	"that mind is awake.</p>\n" &

	"<h3>Two views</h3>\n" &
	"<table class=\"keys\">\n" &
	"<tr><td class=\"k\"><kbd>Esc</kbd></td><td>Drop out of the ship into the <b>editor</b> " &
	"(TED), where you can reshape the world &mdash; sky, fog, view distance and more.</td></tr>\n" &
	"</table>\n" &
	"<p class=\"note\">Controls are still settling as the ship is built. If a key here doesn't " &
	"match the game yet, the game is right &mdash; this page is chasing it.</p>\n"
end function

function ship()
	return
	"<h2>The Ship</h2>\n" &
	"<p>EDEN OS is a <b>game whose setting is a real operating system</b>. That one sentence " &
	"is the whole design. Ask &ldquo;is this an OS?&rdquo; and you get an endless checklist of " &
	"features. Ask &ldquo;is this <i>fun</i>? what does the player feel?&rdquo; and every good " &
	"moment the ship has ever produced comes back &mdash; so <i>game</i> is the compass.</p>\n" &

	"<h3>The conceit</h3>\n" &
	"<p>A computer is an extension of your mind. So the ship <b>is</b> your mind: folders are " &
	"memory rooms, the crew are faculties given a voice, connections are associations. It " &
	"shape-shifts because <i>you</i> add and subtract and design rooms &mdash; by living in " &
	"your real files.</p>\n" &

	"<h3>Inside and outside</h3>\n" &
	"<p>The <b>inside</b> of the ship is procedural: it must be, because it is you, built live " &
	"from your filesystem. The <b>outside</b> &mdash; the starfield, the nebulae, the sectors " &
	"and planets &mdash; is authored, a place the ship sits inside. Travel between sectors and " &
	"the universe changes around you. The ship never actually moves; your mind is the fixed " &
	"point everything else turns about.</p>\n" &

	"<h3>Why it matters</h3>\n" &
	"<p>The productivity is <b>real</b> &mdash; the crew move real files, a sealed door guards " &
	"a real folder &mdash; so the fiction carries real stakes. That is rare, and it is the " &
	"reason to build it.</p>\n"
end function

function bots()
	return
	"<h2>The Crew</h2>\n" &
	"<p>The minds aboard are <b>local AI models</b> running on your own machine &mdash; no " &
	"cloud required. Each is given a <b>persona</b>: the same model can voice many different " &
	"crew members just by changing the character sheet it reads.</p>\n" &

	"<h3>Speaking with them</h3>\n" &
	"<p>Click a crew member, type, press <kbd>Enter</kbd>. They can do more than talk &mdash; " &
	"they can look around, walk to you, follow, pick things up, and act on the world. You give " &
	"them a simple intent (&ldquo;come here&rdquo;) and the ship works out the steps.</p>\n" &

	"<h3>Waking and sleeping</h3>\n" &
	"<p>A mind uses memory while it is awake, so the crew sleep by default. Fly to a " &
	"<b>server rack</b> and flip its switch to wake one; the light turns green. One rack can " &
	"power several crew. Idle minds drift back to sleep to free the ship's memory.</p>\n" &

	"<p class=\"note\">Some crew are for grown-ups only. The ship knows the difference, and so " &
	"should you.</p>\n"
end function

-- =================  ship's log (EDS guestbook)  =========================

function html_escape(sequence s)
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
	sequence pairs = split(body, '&')
	for i = 1 to length(pairs) do
		integer eq = find('=', pairs[i])
		if eq and equal(pairs[i][1..eq-1], key) then
			return url_decode(pairs[i][eq+1..$])
		end if
	end for
	return ""
end function

procedure open_log()
	if db_open("shiplog.edb", DB_LOCK_NO) != DB_OK then
		if db_create("shiplog.edb", DB_LOCK_NO) != DB_OK then
			puts(2, "shiplog: could not create shiplog.edb\n")
			abort(1)
		end if
		if db_create_table("entries") != DB_OK then
			puts(2, "shiplog: could not create table\n")
			abort(1)
		end if
	end if
	if db_select_table("entries") != DB_OK then
		puts(2, "shiplog: could not select table\n")
		abort(1)
	end if
end procedure

procedure add_entry(sequence name, sequence message)
	name = trim(name)
	message = trim(message)
	if length(name) = 0 then name = "a nameless traveller" end if
	if length(name) > 40 then name = name[1..40] end if
	if length(message) > 2000 then message = message[1..2000] end if
	if length(message) = 0 then
		return
	end if
	sequence when = dt:format(dt:now(), "%Y-%m-%d %H:%M")
	integer key = db_table_size() + 1
	if db_insert(key, {name, message, when}) != DB_OK then
	end if
end procedure

function log_page()
	sequence entries = ""
	integer n = db_table_size()
	if n = 0 then
		entries = "<p class=\"note\"><i>The log is empty. Be the first to write in it.</i></p>\n"
	else
		for rec = n to 1 by -1 do
			object d = db_record_data(rec)
			entries &=
				"<p><b>" & html_escape(d[1]) & "</b> " &
				"<span class=\"note\">(" & html_escape(d[3]) & ")</span><br>\n" &
				html_escape(d[2]) & "</p>\n"
		end for
	end if
	return
	"<h2>Ship's Log</h2>\n" &
	"<p>Leave a mark. This page is backed by <b>EDS</b>, Euphoria's own database &mdash; " &
	"your words are written to <code>shiplog.edb</code> and will outlast the server.</p>\n" &
	"<form action=\"/sign\" method=\"post\">\n" &
	"<table class=\"keys\">\n" &
	"<tr><td class=\"k\">Name</td>" &
	"<td><input type=\"text\" name=\"name\" size=\"32\" maxlength=\"40\"></td></tr>\n" &
	"<tr><td class=\"k\">Entry</td>" &
	"<td><textarea name=\"message\" rows=\"4\" cols=\"44\"></textarea></td></tr>\n" &
	"<tr><td></td><td><input type=\"submit\" value=\"Write in the log\"></td></tr>\n" &
	"</table>\n</form>\n" &
	"<hr>\n<h3>Entries (" & sprintf("%d", n) & ")</h3>\n" &
	entries
end function

-- =================  routing + HTTP  =====================================

function route(sequence path)
	if    equal(path, "/")      then return {200, layout("Bridge",      home())}
	elsif equal(path, "/play")  then return {200, layout("How to Play", play())}
	elsif equal(path, "/ship")  then return {200, layout("The Ship",    ship())}
	elsif equal(path, "/bots")  then return {200, layout("The Crew",    bots())}
	elsif equal(path, "/log")   then return {200, layout("Ship's Log",  log_page())}
	else
		return {404, layout("Lost in space",
			"<h2>404 &mdash; Lost in space</h2>\n" &
			"<p>There is no room at <code>" & html_escape(path) & "</code>. " &
			"<a href=\"/\">Beam back to the bridge.</a></p>\n")}
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

function image_response(sequence bytes, sequence ctype)
	return "HTTP/1.1 200 OK\r\n" &
		"Content-Type: " & ctype & "\r\n" &
		"Content-Length: " & sprintf("%d", length(bytes)) & "\r\n" &
		"Connection: close\r\n\r\n" & bytes
end function

procedure main()
	sock:socket server = sock:create(sock:AF_INET, sock:SOCK_STREAM, 0)
	sock:set_option(server, sock:SOL_SOCKET, sock:SO_REUSEADDR, 1)
	if sock:bind(server, BIND_ADDR) != sock:OK then
		printf(2, "EDEN OS site: could not bind %s (error %d) -- is the port in use?\n",
			{BIND_ADDR, sock:error_code()})
		abort(1)
	end if
	open_log()
	puts(1, "EDEN OS is serving at http://localhost:8091   (Ctrl-C to stop)\n")

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
				add_entry(form_value(body, "name"), form_value(body, "message"))
				full = "HTTP/1.1 303 See Other\r\n" &
					"Location: /log\r\n" &
					"Content-Length: 0\r\nConnection: close\r\n\r\n"
			elsif (match(".png", path) or match(".jpg", path) or match(".jpeg", path))
			      and not match("..", path) then
				object bytes = slurp_bytes("." & path)
				if sequence(bytes) then
					sequence ctype = "image/png"
					if match(".jpg", path) or match(".jpeg", path) then
						ctype = "image/jpeg"
					end if
					full = image_response(bytes, ctype)
				else
					full = response(404, layout("Not Found",
						"<h2>404</h2><p>No image at <code>" & html_escape(path) & "</code>.</p>\n"))
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
