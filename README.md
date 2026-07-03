# OmniStem Studio

A native Windows AI stem editor with ensemble source separation, spectral correction, audio-to-MIDI, non-destructive note objects, and OmniScript automation.

## Architecture

- `apps/desktop`: C++20 Windows-native application and audio-domain core
- `services/ai-worker`: Python inference worker
- `scripts`: OmniScript examples
- `docs`: product and engineering architecture

## Current foundation

The first milestone provides a buildable C++ domain prototype, a JSON-lines AI worker, a reversible note-event model, and an initial OmniScript example.

### Build desktop prototype

```powershell
cmake -S apps/desktop -B build/desktop
cmake --build build/desktop --config Release
.\build\desktop\Release\OmniStemDesktop.exe
```

### Run AI worker

```powershell
python services/ai-worker/main.py
```

Input example:

```json
{"id":"job-1","method":"health.check","params":{}}
```

## Planned production stack

C++20, JUCE, CMake, Python 3.11+, PyTorch/ONNX Runtime, SQLite, JSON Lines over local named pipes, VST3/CLAP, ASIO/WASAPI.

## Clean-room requirement

This project must use original source code, UX, data formats, DSP, and model orchestration. It must not copy RipX code, proprietary algorithms, private APIs, branding, or protected assets.

## License

No license has been selected. All rights are reserved until the repository owner adds one.
