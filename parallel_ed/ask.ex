-- ask.ex  --  ask the local model a question from the command line.
--   eui ask.ex "why are sequences nice?"
-- With no argument, it prompts you for one.

include llm.e

sequence args = command_line()   -- {interpreter, script, arg1, arg2, ...}
sequence prompt

if length(args) >= 3 then
	prompt = args[3]
	for i = 4 to length(args) do
		prompt &= " " & args[i]
	end for
else
	puts(1, "ask> ")
	prompt = gets(0)              -- read a line from stdin
end if

puts(1, "\n")
sequence answer = chat(prompt)
if length(answer) = 0 then
	puts(1, "(no reply -- is Ollama running on " & get_model() & "?)\n")
else
	puts(1, answer & "\n")
end if
