# AGENTS.md

This file is the operational contract for AI coding agents working in
`qwen3-tts-bridge-cpp`.

The project is a Windows-first C++17 bridge for local Qwen3-TTS streaming
inference. The public surface should feel like a normal native C++ library,
while the implementation keeps a Python/CUDA worker process alive between
synthesis requests.

## Project Objective

Build a C++17 client library and examples that can start, supervise, and talk to
a standalone Qwen3-TTS worker executable.

The project must not rewrite Qwen3-TTS in C++ during the initial
implementation. The Python worker owns PyTorch, CUDA, model loading, warmup,
and model-specific streaming. The C++ side owns the public API, process
supervision, transport, protocol parsing, request state, callback dispatch,
cancellation, and delivery of PCM chunks to the application.

The first transport is stdin/stdout to a persistent local worker process. A
WebSocket transport is planned later and must be possible without redesigning
the public API or protocol layer.

## Always Follow

- Keep diffs minimal and focused.
- Prefer simple, explicit C++17 over generic abstractions.
- Use the C++17 standard library where it is clear and sufficient.
- Do not add a C++ dependency just for convenience.
- If an extra dependency could materially speed up or simplify the code, ask the
  user before adding it and name the exact library.
- Do not add Boost only for process handling, IPC, JSON, or WebSocket support.
- Keep the worker alive between synthesis requests.
- Do not reload the model for every request.
- Keep transport, protocol, worker supervision, and synthesis request logic
  separated.
- Do not introduce network-accessible APIs in the first implementation.
- Treat WebSocket support as a future local transport, not as phase-1 scope.
- Do not commit model weights, virtual environments, generated build outputs, or
  packaged release artifacts.

## Codebase Discovery

This repository uses codebase-memory-mcp for codebase discovery when an index is
available.

Preferred sequence:

```text
index_status
search_graph / trace_path / get_code_snippet
targeted file reads
```

Use grep/glob/file-search only when:

- searching string literals, error messages, config values, or non-code files;
- the repository is not indexed yet;
- MCP results are insufficient and need precision confirmation.

## Style References

When making decisions about C++ style, naming, public API shape, architecture,
or commit messages, use these local projects as references:

```text
E:/_repoz/log-it-cpp
E:/_repoz/kurlyk
E:/_repoz/mgc-platform
```

Observed conventions to preserve here:

- CMake-based C++17 projects.
- Small composable public interfaces.
- Public classes use PascalCase names.
- Private data members may use the `m_` prefix.
- Prefer RAII ownership and deterministic shutdown.
- Prefer `enum class`, fixed-width integers, and explicit result structures.
- Public APIs should document thread-safety and blocking behavior.
- Comments should be concise and useful; avoid comments that merely restate the
  next line of code.
- Use Doxygen comments for stable public APIs once they exist.
- Keep docs and commit headers in English.
- Use Conventional Commits: `type(scope): summary`.

Commit examples:

```text
feat(transport): add stdio worker transport
fix(protocol): reject oversized frames
docs(agent): document worker lifecycle rules
test(protocol): cover fragmented frame parsing
chore(submodule): add tiny-process-library
```

## Repository Layout

Target layout:

```text
CMakeLists.txt
src/                    C++ headers and sources together
worker/                 Python worker implementation
external/cpp/           C++ dependencies as git submodules
external/python/        vendored or patched Python source trees as git submodules
scripts/                setup, build, packaging, diagnostics
config/                 runtime configuration examples
tests/                  C++, Python, mock-worker, and integration tests
models/                 local model storage, not committed
dist/                   generated release packages, not committed
```

C++ `.hpp` and `.cpp` files live together in `src/` unless the project later
gets a deliberate public `include/` layout.

## Dependency Policy

All source dependencies must be git submodules. Keep them linear and avoid
recursive submodules unless there is no practical alternative.

Initial approved dependency:

```text
external/cpp/tiny-process-library/
    https://gitlab.com/eidheim/tiny-process-library
```

Python Qwen source dependency:

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

Pin every submodule to a specific commit. Document dependency purpose and pin
reason when dependency docs are added.

Normal Python packages belong in locked requirement files, not in
`external/python/`.

Recommended Python dependency files:

```text
worker/requirements.in
worker/requirements.lock.txt
```

Do not store an installed Python runtime in `external/python/`. The packaged
runtime belongs to generated `dist/` output produced by Nuitka.

## Public C++ API Direction

The public API should hide the worker process and expose a persistent client.

Example target usage:

```cpp
QwenTtsClient tts;

tts.start("qwen_tts_worker.exe");
tts.synthesize("First phrase", on_audio);
tts.synthesize("Second phrase", on_audio);
tts.synthesize("Third phrase", on_audio);

tts.stop();
```

The implementation must start the worker once, keep it running, and send many
requests through the same worker session.

The public API should also support a more explicit request object:

```cpp
struct TtsRequest {
    std::uint64_t id = 0;
    std::string text;              // UTF-8
    std::string language = "auto";
    std::string speaker = "default";
    std::string instruction;       // emotion, whispering, prosody, etc.
};
```

Do not hardcode a closed set of emotions in C++ for the first version. Qwen3-TTS
supports natural-language voice and style control, so the bridge should pass
UTF-8 text and instructions through to the worker without interpreting them.

Do not mix service instructions into `text` by inventing tags such as
`[angry]`. Keep spoken text and natural-language control separate in the
protocol.

Expected model-family mapping:

```text
CustomVoice
    text + language + speaker + instruction

VoiceDesign
    text + language + instruction as voice/style description

Base / voice clone
    text + language + reference audio/prompt data
    instruction support must be treated as model-dependent until verified
```

Optional C++ convenience helpers may map an `Emotion` enum to instruction text,
but the primary API must remain open-ended through `request.instruction`.

Treat singing as an engine capability to validate in the worker. The C++ API
should be flexible enough to pass lyrics, prompts, or engine-specific options,
but must not promise singing-specific behavior until tested.

## C++ Architecture

Logical components:

```text
QwenTtsClient
    public library facade

WorkerSession
    owns worker lifetime, transport, protocol, dispatcher, request registry

ITransport
    byte transport abstraction used by protocol

StdIoTransport
    phase-1 implementation over worker stdin/stdout using tiny-process-library

WebSocketTransport
    future implementation over Simple-WebSocket-Server/asio

Protocol
    frame serialization, parsing, validation, version checks

RequestRegistry
    request IDs, request state, callbacks, cancellation, terminal events

Dispatcher
    moves parsed events away from the transport thread and invokes callbacks

AudioQueue
    buffers PCM chunks and applies backpressure policy
```

Dependency direction:

```text
QwenTtsClient
    -> WorkerSession
        -> ITransport
        -> Protocol
        -> RequestRegistry
        -> Dispatcher
```

`Protocol` must not depend on process management, Python, CUDA, or Qwen-specific
code.

`ITransport` must not know the semantic meaning of synthesis requests.

`QwenTtsClient` must not parse model-specific internals.

## Transport Contract

Use a transport abstraction that works for stdio now and WebSocket later. Keep
it byte-oriented and protocol-neutral.

Suggested shape:

```cpp
class ITransport {
public:
    using Bytes = std::vector<std::byte>;
    using ReceiveHandler = std::function<void(Bytes)>;
    using ErrorHandler = std::function<void(std::string)>;
    using ExitHandler = std::function<void(int)>;

    virtual ~ITransport() = default;

    virtual bool start(
        ReceiveHandler receive_handler,
        ErrorHandler error_handler,
        ExitHandler exit_handler) = 0;

    virtual bool send(const std::byte* data, std::size_t size) = 0;
    virtual bool is_running() const = 0;
    virtual void stop() = 0;
};
```

The exact interface may change during implementation, but preserve these
properties:

- transport receives and sends bytes only;
- receive callbacks are internal callbacks, not user synthesis callbacks;
- transport errors are reported separately from worker/model errors;
- read boundaries are arbitrary and must not be treated as protocol frames;
- `stop()` is deterministic and safe to call more than once;
- no mutex is held while invoking a user callback.

For `StdIoTransport`:

- stdin carries client-to-worker protocol frames;
- stdout carries worker-to-client protocol frames and binary PCM frames;
- stderr carries worker logs only;
- the worker must never write human-readable logs to stdout;
- stdout writes must be binary, flushed, and frame-based;
- do not use line-oriented parsing for protocol traffic.

## Python Worker Architecture

Logical components:

```text
main.py
    entry point and argument parsing

stdio_server.py
    stdin/stdout IPC server and request lifecycle

protocol.py
    frame serialization, parsing, and validation

engine.py
    TtsEngine interface and engine errors

mock_engine.py
    deterministic PCM test engine

qwen_engine.py
    adapter around Qwen3-TTS-streaming

config.py
    runtime configuration

logging_setup.py
    stderr/file logging setup
```

The IPC server must not import internal Qwen model classes directly. Keep Qwen
behind a narrow engine interface:

```python
class TtsEngine:
    def load(self) -> None:
        ...

    def warmup(self) -> None:
        ...

    def synthesize_stream(self, request):
        yield pcm_chunk

    def cancel(self, request_id: int) -> None:
        ...

    def close(self) -> None:
        ...
```

Keep the mock engine available after Qwen integration so protocol, transport,
and packaging tests can run without CUDA.

## Protocol Requirements

Create a versioned protocol before adding real Qwen integration.

The first protocol version may be:

```text
QWEN_TTS_BRIDGE_PROTOCOL = 1
```

Every control payload must contain:

```text
protocol_version
type
request_id
```

Use framed messages. Never rely on one `read()` matching one logical message.

Suggested frame header fields:

```text
magic
protocol_version
frame_type
flags
payload_size
request_id
```

Use fixed-width integer fields and define byte order explicitly.

Control payloads may be UTF-8 JSON. PCM data must use binary frames. Do not
encode audio as Base64.

If C++ JSON parsing becomes more than a narrow known-field parser, ask the user
before adding a JSON dependency such as `nlohmann/json`.

## Request Lifecycle

A synthesis request has these states:

```text
created
accepted
running
completed
cancelled
failed
```

Allowed transitions:

```text
created -> accepted
accepted -> running
running -> completed
running -> cancelled
running -> failed
accepted -> cancelled
accepted -> failed
```

A request must produce exactly one terminal state. Late audio chunks received
after a terminal state must be discarded and logged.

## Threading Requirements

The transport read loop must not invoke user callbacks directly.

Recommended C++ flow:

```text
transport read callback
    -> protocol parser
    -> event queue
    -> dispatcher thread
    -> user callback
```

Protect request state with explicit synchronization. Do not hold a mutex while
calling application callbacks.

Document which public methods block and which are safe to call from callbacks.

Worker shutdown must be deterministic:

```text
send shutdown request
stop accepting new synthesis requests
drain or cancel active requests
wait for worker exit up to timeout
terminate as fallback
join transport and dispatcher threads
```

## Error Model

Separate error categories:

```text
transport_error
protocol_error
worker_error
model_error
request_error
cancelled
timeout
```

Every error should include:

```text
category
code
message
request_id
```

Do not use worker process termination as the normal way to signal synthesis
errors.

## Configuration

Runtime configuration should support at least:

```text
model_path
device
dtype
sample_rate
worker_executable
worker_arguments
worker_working_directory
transport
log_level
warmup_enabled
compile_enabled
cuda_graphs_enabled
```

Configuration should be loadable from JSON and overridable by command-line
arguments. Do not hardcode local absolute paths.

## Logging

C++ and Python logs must be separate.

The Python worker must write logs to stderr or a log file. It must never write
logs into stdout because stdout is reserved for protocol frames.

Every log entry related to a request should include its request ID.

Important lifecycle events:

```text
worker starting
model loading
model loaded
warmup started
warmup completed
client connected
request accepted
first PCM chunk produced
request completed
request cancelled
request failed
worker shutting down
```

## Build Policy

C++ build:

```text
CMake
C++17
Windows x64
```

Python worker build:

```text
Nuitka
standalone directory mode
Windows x64
```

Do not target Nuitka onefile mode initially. The model directory must remain
external to the packaged worker.

Generated release layout should resemble:

```text
dist/QwenTTSBridge/
    qwen_tts_client.exe
    worker/
        qwen_tts_worker.exe
        required runtime files
    config/
        qwen_tts.json
    models/
```

## Testing Strategy

Create tests in this order.

### Phase 1: Protocol Tests

- serialize and parse control messages;
- parse fragmented frames;
- parse multiple frames in one read;
- reject malformed frame headers;
- reject unsupported protocol versions;
- validate payload size limits.

### Phase 2: Mock Worker Tests

- start and stop worker;
- exchange health-check messages over stdio;
- simulate PCM streaming;
- simulate worker errors;
- simulate unexpected worker termination;
- verify stderr logging does not corrupt stdout protocol frames.

### Phase 3: Request Lifecycle Tests

- submit multiple sequential requests without restarting the worker;
- preserve PCM chunk order per request;
- deliver exactly one terminal event;
- discard late chunks after terminal state;
- cancel active synthesis;
- recover after cancellation.

### Phase 4: Qwen Integration Tests

- load the configured model;
- synthesize a short phrase;
- validate non-empty PCM output;
- validate sample rate and channel count;
- execute multiple requests without reloading the model;
- pass natural-language instructions such as emotion or whispering to the
  worker.

### Phase 5: Packaging Tests

- start the worker on a machine without system Python;
- locate external model files;
- locate CUDA and PyTorch runtime libraries;
- report missing runtime dependencies clearly.

## Implementation Sequence

Follow this order unless a blocking technical reason requires a change.

1. Create repository skeleton and baseline CMake.
2. Add submodule paths for approved dependencies.
3. Define protocol data structures independently of Qwen3-TTS.
4. Add protocol unit tests.
5. Implement the Python mock worker over stdin/stdout.
6. Implement `StdIoTransport` with tiny-process-library.
7. Add a minimal console example that writes received PCM into a WAV file.
8. Add request tracking, callback dispatch, and cancellation.
9. Introduce the Python `TtsEngine` abstraction.
10. Integrate `external/python/Qwen3-TTS-streaming/`.
11. Add model loading, CUDA device selection, and full-audio synthesis.
12. Add incremental PCM streaming.
13. Measure startup time, model load time, warmup time, first-audio latency,
    real-time factor, and peak VRAM.
14. Add Nuitka packaging in standalone directory mode.
15. Add production supervision: heartbeat, startup timeout, graceful shutdown,
    forced termination fallback, stderr capture, and diagnostic exit codes.
16. After correctness and packaging are stable, evaluate `torch.compile`,
    static KV cache, CUDA Graphs, reduced precision, pinned host memory, larger
    PCM chunks, and alternative transports.

## Definition of Done for the First Usable Version

The first usable version is complete when:

- the C++ example starts the packaged worker;
- the worker loads Qwen3-TTS once;
- the C++ client sends a synthesis request;
- PCM chunks are returned incrementally;
- the chunks can be saved into a valid WAV file;
- cancellation works;
- errors are returned as structured protocol messages;
- the worker can process multiple sequential requests;
- the target machine does not require a separate Python installation;
- setup, build, packaging, and dependency steps are documented.
