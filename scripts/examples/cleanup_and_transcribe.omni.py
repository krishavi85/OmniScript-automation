# OmniScript runs as a restricted command transaction: no imports, files,
# network, process access, attributes, or unrestricted Python built-ins.
for note in notes():
    if note["confidence"] < 0.55:
        mute_note(note["id"])
    elif note["gainDb"] > 3.0:
        set_note_gain(note["id"], 3.0)

for stem in stems():
    if stem["role"] == "lead-vocal":
        request_transcription(stem["id"])
