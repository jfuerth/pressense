# MIDI handling
- Let's refactor to a more general handling of 1 vs 2 data bytes, so we properly trigger an action based on the current running status when the terminal data byte is received. I've added a table of MIDI status messages. It looks like status nybbles 8, 9, A, B, and E always take 2 bytes. We can just clear the running status when we see any Fx status.

