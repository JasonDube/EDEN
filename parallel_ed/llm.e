-- llm.e  --  a tiny, readable LLM client for Euphoria.
--
-- It does NOT run a model.  It sends your text to a model *server* (Ollama by
-- default, running locally) over HTTP as JSON, and hands you back the reply.
-- curl carries the bytes; everything else -- building the JSON request and
-- decoding the JSON reply -- is plain Euphoria you can read end to end.
--
--   include llm.e
--   sequence answer = chat("Explain a sequence in one sentence.")
--
-- Point it elsewhere with set_host()/set_model() (e.g. a cloud endpoint).

include std/math.e      -- floor, and_bits
include std/os.e        -- system, command_line

-- ---------------------------------------------------------------- config ---

sequence llm_host  = "http://localhost:11434"
sequence llm_model = "qwen3.5:9b"

public procedure set_host(sequence url)
	llm_host = url
end procedure

public procedure set_model(sequence name)
	llm_model = name
end procedure

public function get_model()
	return llm_model
end function

-- ------------------------------------------------------------ JSON out -----

function json_escape(sequence s)
-- turn a Euphoria string into the inside of a JSON string (no quotes)
	sequence out = ""
	for i = 1 to length(s) do
		integer c = s[i]
		if c = '"' then
			out &= "\\\""
		elsif c = '\\' then
			out &= "\\\\"
		elsif c = '\n' then
			out &= "\\n"
		elsif c = '\r' then
			out &= "\\r"
		elsif c = '\t' then
			out &= "\\t"
		elsif c < 32 then
			out &= sprintf("\\u%04x", c)
		else
			out &= c
		end if
	end for
	return out
end function

-- ------------------------------------------------------------- JSON in -----

function utf8(atom c)
-- encode one Unicode code point as UTF-8 bytes
	if c < 0x80 then
		return {c}
	elsif c < 0x800 then
		return {0xC0 + floor(c / 0x40), 0x80 + and_bits(c, 0x3F)}
	else
		return {0xE0 + floor(c / 0x1000),
				0x80 + and_bits(floor(c / 0x40), 0x3F),
				0x80 + and_bits(c, 0x3F)}
	end if
end function

function hex4(sequence h)
-- value of a 4-digit hex string
	atom n = 0
	for i = 1 to length(h) do
		integer d = h[i], v
		if d >= '0' and d <= '9' then
			v = d - '0'
		elsif d >= 'a' and d <= 'f' then
			v = d - 'a' + 10
		elsif d >= 'A' and d <= 'F' then
			v = d - 'A' + 10
		else
			v = 0
		end if
		n = n * 16 + v
	end for
	return n
end function

function decode_string(sequence s, integer p)
-- s[p] is the opening quote of a JSON string; return the decoded text
	sequence out = ""
	p += 1
	while p <= length(s) do
		integer c = s[p]
		if c = '"' then
			exit
		elsif c = '\\' and p < length(s) then
			integer e = s[p + 1]
			if e = 'n' then
				out &= '\n'
			elsif e = 't' then
				out &= '\t'
			elsif e = 'r' then
				out &= '\r'
			elsif e = '"' then
				out &= '"'
			elsif e = '\\' then
				out &= '\\'
			elsif e = '/' then
				out &= '/'
			elsif e = 'u' and p + 5 <= length(s) then
				out &= utf8(hex4(s[p + 2 .. p + 5]))
				p += 4
			else
				out &= e
			end if
			p += 2
		else
			out &= c
			p += 1
		end if
	end while
	return out
end function

function json_field(sequence json, sequence name)
-- pull the string value of the first top-level "name":"..." field
	sequence key = "\"" & name & "\":"
	integer p = match(key, json)
	if p = 0 then
		return ""
	end if
	p += length(key)
	while p <= length(json) and (json[p] = ' ' or json[p] = '\n') do
		p += 1
	end while
	if p > length(json) or json[p] != '"' then
		return ""
	end if
	return decode_string(json, p)
end function

-- ------------------------------------------------------------- transport ---

function slurp(sequence fname)
-- read a whole file into a sequence of bytes
	integer f = open(fname, "rb")
	if f = -1 then
		return ""
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

-- ---------------------------------------------------------------- chat -----

public function chat(sequence prompt)
-- send a single prompt to the model, return its text reply ("" on failure)
	sequence req = "{\"model\":\"" & llm_model &
				   "\",\"prompt\":\"" & json_escape(prompt) &
				   "\",\"stream\":false,\"think\":false}"

	integer f = open("llm_req.json", "wb")
	puts(f, req)
	close(f)

	-- curl POSTs the request file and writes the reply to llm_resp.json
	system(sprintf("curl -s %s/api/generate -d @llm_req.json > llm_resp.json 2>/dev/null",
				   {llm_host}), 2)

	sequence resp = slurp("llm_resp.json")
	sequence answer = json_field(resp, "response")
	if length(answer) = 0 then
		-- surface an Ollama error message if there was one
		answer = json_field(resp, "error")
	end if
	return answer
end function
