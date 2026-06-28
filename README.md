# QwenTTSBridge

QwenTTSBridge is a Windows-oriented C++17 client library for local streaming
speech synthesis with Qwen3-TTS.

The project keeps Python, PyTorch, CUDA, and Qwen3-TTS inside a standalone
worker process while exposing a stable native C++ API to applications. The
worker is started once, keeps the model loaded, and streams PCM audio chunks
back to the C++ side for every synthesis request.

Status: early architecture and repository bootstrap.

## Goals

- provide a simple C++17 API for Qwen3-TTS;
- isolate Python, PyTorch, CUDA, and model code in a separate worker process;
- keep the worker and model alive between requests;
- support low-latency streaming PCM output;
- package the worker as a standalone Windows application with Nuitka;
- avoid requiring Python to be installed on the target machine;
- keep the transport architecture open for a future WebSocket transport.

## Non-Goals

The first version does not attempt to:

- rewrite Qwen3-TTS in C++;
- replace PyTorch with LibTorch, ONNX Runtime, or TensorRT;
- embed model weights into the executable;
- provide a remote network API;
- guarantee a single-file distribution.

## Architecture

```text
C++17 application
        |
        | QwenTtsClient public API
        v
C++ bridge library
        |
        | stdin/stdout framed protocol
        v
Qwen TTS worker executable
        |
        v
Python + PyTorch + CUDA
        |
        v
Qwen3-TTS streaming engine
```

The C++ application starts and supervises the worker process. The first
transport uses the worker stdin/stdout streams. Later, the same protocol should
be usable through a WebSocket transport.

The worker:

1. initializes Python and PyTorch;
2. loads the configured Qwen3-TTS model;
3. warms up CUDA execution when enabled;
4. waits for synthesis requests;
5. streams PCM chunks to the client;
6. remains alive for subsequent requests.

## C++ API Direction

The public API should feel like a normal C++ library, not like a command-line
wrapper around Python.

```cpp
QwenTtsClient tts;

tts.start("qwen_tts_worker.exe");
tts.synthesize("First phrase", on_audio);
tts.synthesize("Second phrase", on_audio);
tts.synthesize("Third phrase", on_audio);

tts.stop();
```

The explicit request form should stay extensible:

```cpp
struct TtsRequest {
    std::uint64_t id = 0;
    std::string text;              // UTF-8 text
    std::string language = "auto";
    std::string speaker = "default";
    std::string instruction;       // emotion, whispering, prosody, etc.
};

struct PcmChunk {
    std::uint64_t request_id = 0;
    std::vector<std::int16_t> samples;
    std::uint32_t sample_rate = 24000;
    std::uint16_t channels = 1;
    bool is_final = false;
};
```

Emotional speech, whispering, speaking rate, prosody, and similar controls
should be passed to the worker as UTF-8 text or a natural-language style
instruction. The C++ API should not start with a closed emotion enum because
Qwen3-TTS exposes much of this control through natural-language instructions.
Singing should be treated as an engine capability to validate before the C++ API
promises any singing-specific behavior.

The protocol should keep spoken text and style instruction separate:

```json
{
  "type": "synthesize",
  "request_id": 42,
  "text": "I thought you were not coming.",
  "language": "English",
  "speaker": "default",
  "instruction": "Speak with relief, but keep a little resentment."
}
```

Model families use this control differently:

```text
CustomVoice    text + language + speaker + instruction
VoiceDesign    text + language + instruction as voice/style description
Base/clone     text + language + reference audio or prompt data
```

Optional C++ helpers may map simple presets such as `Emotion::Happy` or
`Emotion::Sad` into instruction strings, but the main API should remain
open-ended through `request.instruction`.

## Transport Plan

The first implementation uses a persistent worker process and framed binary
messages over stdin/stdout:

```text
stdin   client -> worker protocol frames
stdout  worker -> client protocol and PCM frames
stderr  worker logs only
```

Worker logs must never be written to stdout because stdout is reserved for
protocol frames and binary PCM data.

The transport layer should remain byte-oriented:

```cpp
class ITransport {
public:
    using Bytes = std::vector<std::byte>;
    using ReceiveHandler = std::function<void(Bytes)>;

    virtual ~ITransport() = default;

    virtual bool start(ReceiveHandler receive_handler) = 0;
    virtual bool send(const std::byte* data, std::size_t size) = 0;
    virtual bool is_running() const = 0;
    virtual void stop() = 0;
};
```

The exact interface may evolve, but `ITransport` should not know anything about
Qwen, synthesis requests, JSON payloads, or PCM meaning. Protocol parsing and
request state live above the transport.

## Communication Protocol

The protocol must be versioned from the beginning.

Suggested frame header fields:

```text
magic
protocol_version
frame_type
flags
payload_size
request_id
```

Control payloads may be UTF-8 JSON. PCM audio must use binary frames and must
not be Base64-encoded.

Every control message should contain:

```text
protocol_version
type
request_id
```

Request terminal states:

```text
completed
cancelled
failed
```

Every accepted request must produce exactly one terminal event.

## Repository Layout

Planned layout:

```text
src/                    C++17 client implementation
worker/                 Python worker implementation
external/cpp/           C++ dependencies as git submodules
external/python/        vendored or patched Python projects as git submodules
scripts/                setup, build, packaging, and diagnostics scripts
config/                 runtime configuration examples
tests/                  C++, Python, mock-worker, and integration tests
models/                 local model storage, not committed
dist/                   generated release packages, not committed
```

C++ headers and source files are stored together in `src/` during the initial
phase.

## Dependencies

All source dependencies are managed as git submodules and pinned to exact
commits. Keep submodules linear and avoid recursive submodules when practical.

Initial C++ dependency:

```text
external/cpp/tiny-process-library/
https://gitlab.com/eidheim/tiny-process-library
```

Qwen streaming fork:

```text
external/python/Qwen3-TTS-streaming/
https://github.com/NewYaroslav/Qwen3-TTS-streaming
```

Future WebSocket dependencies:

```text
external/cpp/Simple-WebSocket-Server/
https://gitlab.com/eidheim/Simple-WebSocket-Server

external/cpp/asio/
https://github.com/chriskohlhoff/asio
```

Normal Python packages should be installed from locked requirements files and
must not be committed into the repository.

Model weights are stored locally under `models/` and are excluded from Git.

## Build Strategy

The repository will produce two primary artifacts:

```text
qwen_tts_client.exe
qwen_tts_worker.exe
```

The C++ component is built with CMake and a C++17 compiler.

The Python worker is packaged using Nuitka in standalone directory mode.
Onefile packaging is not the initial target because PyTorch and CUDA
distributions are large and often need runtime files next to the executable.

## Planned Milestones

### Milestone 1: Protocol Prototype

- define versioned frames and control messages;
- implement a mock Python worker;
- start the worker from C++;
- exchange health-check messages;
- stream deterministic test PCM data.

### Milestone 2: Persistent Client

- add `QwenTtsClient`;
- keep the worker alive across multiple requests;
- add request IDs, callbacks, cancellation, and terminal events;
- save streamed PCM into a WAV file from a C++ example.

### Milestone 3: Qwen3-TTS Integration

- integrate `external/python/Qwen3-TTS-streaming/`;
- load Qwen3-TTS in the Python worker;
- synthesize complete audio;
- pass natural-language style instructions to the engine;
- add structured model and worker errors.

### Milestone 4: Streaming and Packaging

- expose incremental PCM streaming from the Qwen engine;
- measure first-audio latency and real-time factor;
- package the worker with Nuitka;
- verify the packaged worker runs without system Python.

### Milestone 5: Production Transport

- add heartbeat, startup timeout, and graceful shutdown;
- capture stderr diagnostics;
- add forced termination fallback;
- evaluate WebSocket transport with Simple-WebSocket-Server and asio.

## References

- Qwen3-TTS upstream: https://github.com/QwenLM/Qwen3-TTS
- Qwen3-TTS streaming fork: https://github.com/NewYaroslav/Qwen3-TTS-streaming
- Qwen3-TTS streaming documentation: https://qwenlm-qwen3-tts.mintlify.app/guides/streaming
