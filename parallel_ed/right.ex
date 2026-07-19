-- Euphoria  --  split a sentence into words
include std/sequence.e   -- for split()

sequence text = "All that we are is the result of what we have thought"

sequence words = split(text, " ")

for i = 1 to length(words) do
    puts(1, words[i] & "\n")
end for
