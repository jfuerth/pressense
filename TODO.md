# MIDI handling
- Let's refactor to a more general handling of 1 vs 2 data bytes, so we properly trigger an action based on the current running status when the terminal data byte is received. I've added a table of MIDI status messages. It looks like status nybbles 8, 9, A, B, and E always take 2 bytes. We can just clear the running status when we see any Fx status.

# Voice allocator
- don't steal the lowest or highest note
- remember stolen notes and put them back when a slot is available?
  - would need to remember the synth's state - ADSR, delays, filters; not just retrigger

# Program save/load
- Try being less verbose with the JSON mapping.
  - Could we make the JSON really mirror the structure of the voice, nesting filter settings in the JSON (recursive serialization)? Does the nlohmann library support this? Could the synth objects themselves be JSON serializable? Ideally programs could represent synths with different structures of filters, envelopes, etc, and build them from the JSON...