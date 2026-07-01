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

The first transport is stdin/stdout to a persistent local worker process. The
first implementation line should target the async-first `0.2` shape rather than
a sync-only `0.1` API. A WebSocket transport is planned later and must be
possible without redesigning the public API or protocol layer.

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
- Design the C++ API as async-first: request submission returns quickly, audio
  arrives through callbacks, and cancellation is request-ID based.
- Treat synchronous helpers as optional wrappers over the async core.
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

When committing, keep logical layers separate where practical: code changes,
agent/project rules, dependency updates, and generated documentation should be
separate commits unless they are inseparable. Run the relevant local tests or
state clearly why they were not run before committing.

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

## C++ Source Organization

All project C++ headers and sources should live under:

```text
src/qwen_tts_bridge/
```

Prefer includes that name the library domain explicitly:

```cpp
#include <qwen_tts_bridge/protocol/framing.hpp>
```

Do not add new root-level headers such as `src/Protocol.hpp` or
`src/Client.hpp`. Keep names meaningful for downstream users and keep the
source tree shaped like the namespace.

Split C++ code by subdomain instead of growing catch-all files:

```text
data/                 shared enums, DTOs, and lightweight value types
protocol/framing/     QTB binary frame codec and incremental parser
protocol/control/     future JSON control-message validation/mapping
transport/            byte transports such as stdio and future websocket
client/               public facade and user-facing request API
session/              worker lifetime, queues, dispatch, request registry
```

Use umbrella headers sparingly but consistently at subdomain boundaries:

```text
qwen_tts_bridge/data.hpp
qwen_tts_bridge/protocol/framing.hpp
```

Prefer umbrella headers as the public connection points between domains and
layers. Public headers and examples should include the stable domain surface,
for example:

```cpp
#include <qwen_tts_bridge/audio.hpp>
#include <qwen_tts_bridge/client.hpp>
#include <qwen_tts_bridge/transport.hpp>
```

Inside `src/qwen_tts_bridge`, umbrella headers should include project leaf
headers with local quoted paths:

```cpp
#include "transport/ITransport.hpp"
#include "transport/stdio/StdIoTransport.hpp"
```

Prefer that over project-qualified leaf includes such as
`<qwen_tts_bridge/transport/ITransport.hpp>` inside the library's own umbrella
headers.

Avoid direct leaf-to-leaf project includes in public headers when a forward
declaration or a higher-level umbrella header can express the dependency. It is
acceptable that umbrella headers include related leaf headers in advance and
that some leaf headers are not fully standalone; this follows the style used by
the reference libraries. Implementation-private headers inside one subdomain
may still be included directly by that subdomain's `.cpp` files.

Concrete implementations should be named after their responsibility, for
example `FrameParser`, `FrameCodec`, `StdIoTransport`, or `RequestRegistry`.
Avoid naming a file or class `Protocol` unless it truly owns the whole protocol
surface.

Keep enum classes and basic DTOs in `data/` or in the nearest subdomain
`data/` file. If several files need the same DTO group, expose them through an
umbrella header instead of including many leaf headers everywhere.

Use Doxygen comments for stable public or cross-subdomain C++ APIs. Internal
helpers may stay undocumented when the code is self-explanatory, but exported
classes, structs, enums, and non-trivial functions should have concise
`\brief`, `\param`, and `\return` comments where useful.

Use `detail` only for genuinely private implementation helpers. Do not use
`utils`, `helpers`, or `details` as dumping grounds for domain logic that should
belong to `protocol`, `transport`, `client`, or `session`.

Tests should mirror the subdomain they cover. For example, binary frame tests
belong in protocol-focused test files, while worker lifecycle tests should stay
separate from codec tests.

## Dependency Policy

All source dependencies must be git submodules. Keep them linear and avoid
recursive submodules unless there is no practical alternative.

Initial approved dependency:

```text
external/cpp/tiny-process-library/
    https://gitlab.com/eidheim/tiny-process-library
```

Approved C++ JSON dependency:

```text
external/cpp/nlohmann-json/
    https://github.com/nlohmann/json
```

Keep `nlohmann/json` private to implementation files. Public headers must
continue to expose project DTOs and result types, not JSON-library types.
When the project gets a deliberate public `include/` or install layout, move
private headers that include `nlohmann/json` or expose `control_detail`
internals out of the public include path.

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
worker/requirements-dev.in
worker/requirements-dev.lock.txt
```

Development-only tools such as Ruff and Pyright belong in
`worker/requirements-dev.in` and `worker/requirements-dev.lock.txt`. Keep
runtime worker dependencies separate from dev tooling.

Use the project scripts for Python tooling checks when they are available:

```text
scripts/setup-python-dev.ps1
scripts/check-python.ps1
```

For isolated local setup, pass `-UseVenv` to both scripts. In GitHub Actions,
prefer the Python executable from `actions/setup-python`:

```text
scripts/setup-python-dev.ps1 -Python python
scripts/check-python.ps1 -Python python
```

Future CI hardening notes:

- Pin GitHub Actions by full commit SHA only when the project starts enforcing
  a stricter supply-chain policy.
- Do not prioritize Linux Python CI jobs while the project remains
  Windows-first. Add Linux coverage later only if the worker is expected to be
  developed or shipped cross-platform.

Do not store an installed Python runtime in `external/python/`. The packaged
runtime belongs to generated `dist/` output produced by Nuitka.

## Public C++ API Direction

The public API should hide the worker process and expose a persistent async
client.

Example target usage:

```cpp
QwenTtsClient tts;

tts.start("qwen_tts_worker.exe");
const auto first_id = tts.synthesize_async("First phrase", first_callbacks);
const auto second_id = tts.synthesize_async("Second phrase", second_callbacks);
const auto third_id = tts.synthesize_async("Third phrase", third_callbacks);

tts.cancel(second_id);

tts.stop();
```

The implementation must start the worker once, keep it running, and send many
requests through the same worker session. `synthesize_async()` must not block on
model inference; it should return after the request is accepted into the local
client pipeline or fail fast if it cannot be accepted.

The public API should also support a more explicit request object:

```cpp
struct TtsRequest {
    std::uint64_t id = 0;
    std::string text;              // UTF-8
    std::string language = "auto";
    std::string speaker;           // optional per-request voice/speaker id
    std::string instruction;       // emotion, whispering, prosody, etc.
};
```

Target callback API shape:

```cpp
using RequestId = std::uint64_t;

struct TtsCallbacks {
    std::function<void(const PcmChunk&)> on_audio;
    std::function<void()> on_completed;
    std::function<void(const TtsError&)> on_error;
    std::function<void()> on_cancelled;
};

class QwenTtsClient {
public:
    RequestId synthesize_async(
        TtsRequest request,
        TtsCallbacks callbacks);

    bool cancel(RequestId request_id);
};
```

If a synchronous API is added, implement it as a small wrapper that waits on the
async callbacks. Do not let the synchronous wrapper define protocol, transport,
or worker design.

Do not hardcode a closed set of emotions in C++ for the first version. Qwen3-TTS
supports natural-language voice and style control, so the bridge should pass
UTF-8 text and instructions through to the worker without interpreting them.

Do not mix service instructions into `text` by inventing tags such as
`[angry]`. Keep spoken text and natural-language control separate in the
protocol.

Treat `speaker` as an optional per-request voice override, not as a universal
`"default"` sentinel. Empty means the application did not select a voice. Do
not send `"default"` to Qwen CustomVoice as if it were a real supported speaker
unless that exact speaker name was advertised by the model.

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

OutgoingRequestQueue
    accepts async submissions and feeds the writer thread
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

The C++ runtime model should be:

```text
application threads
    -> QwenTtsClient::synthesize_async()
    -> outgoing request queue
    -> writer thread
    -> worker stdin

worker stdout
    -> transport reader thread/callback
    -> Protocol frame parser
    -> incoming event queue
    -> dispatcher thread
    -> user callbacks
```

All frames and events must carry `request_id` so responses can be routed to the
correct callback set.

## Transport Contract

Use a transport abstraction that works for stdio now and WebSocket later. Keep
it byte-oriented and protocol-neutral.

Suggested shape:

```cpp
enum class SendResult {
    Accepted,
    WouldBlock,
    Closed,
    Failed
};

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

    virtual SendResult send(const std::byte* data, std::size_t size) = 0;
    virtual bool is_running() const = 0;
    virtual void stop() = 0;
};
```

The exact interface may change during implementation, but preserve these
properties:

- transport receives and sends bytes only;
- receive callbacks are internal callbacks, not user synthesis callbacks;
- send results distinguish accepted bytes, temporary backpressure, closed
  transports, and local send failures;
- until `WorkerSession` gets an outbound queue, retry policy, and writable
  notifications, it must treat every non-`Accepted` send result, including
  `WouldBlock`, as a terminal session transport failure;
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
- do not confuse stdio transport with synchronous behavior; stdio is fully
  compatible with an async request API and streaming callbacks.

## Python Worker Architecture

Logical components:

```text
main.py
    entry point and argument parsing

stdio_server.py
    stdin/stdout IPC server, request queue, and request lifecycle

protocol/
    byte-level protocol package

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

Keep the Python worker source split by subdomain, the same way as the C++ side.
Do not grow a monolithic `protocol.py`.

The worker is an installable Python package that lives under:

```text
worker/src/qwen_tts_bridge_worker/
```

Do not make tests import worker modules by mutating `sys.path`. Prefer running
Python tests against an editable install, or set `PYTHONPATH=worker/src` only in
the test runner or subprocess harness when an editable install is not available.

Follow normal Python style:

- use PEP 8 naming: `PascalCase` classes, `snake_case` functions and variables,
  `UPPER_SNAKE_CASE` constants;
- keep imports grouped as standard library, third-party packages, then project
  imports;
- use type hints on public and cross-module functions;
- use concise docstrings for public modules, classes, and non-trivial
  functions;
- write comments for why something is necessary, not to restate the next line;
- prefer plain functions over classes when there is no state, lifecycle,
  polymorphism, or injected dependency;
- prefer `dataclass(frozen=True, slots=True)` for immutable DTOs and validate
  their invariants in `__post_init__`;
- use `collections.abc` collection protocols for type hints where practical;
- do not add a heavy DDD/Clean Architecture skeleton just for ceremony.

Use layered boundaries without overengineering:

```text
CLI/config composition
    -> server/application lifecycle
        -> TtsEngine protocol
        -> protocol framing/control mapping
```

The engine layer should not know wire-level protocol categories, JSON payload
shape, stdin/stdout details, or CLI parsing. The server/protocol boundary maps
engine-domain failures to protocol errors.

Preferred protocol package layout:

```text
worker/src/qwen_tts_bridge_worker/protocol/
    __init__.py          umbrella import surface only
    data/                enums, constants, DTOs, and parse results
    framing/             QTB binary frame codec and incremental parser
    control/             future JSON control-message validation/mapping
```

The package-level `protocol/__init__.py` may re-export stable names used by the
server, but implementation logic belongs in the nearest subdomain module.

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

The worker should accept many requests but may run model inference
sequentially in the first implementation:

```text
stdin reader thread / async task
    accepts synthesize and cancel commands

worker request queue
    stores queued synthesis work

engine thread
    runs one inference request at a time

stdout writer queue
    serializes protocol and PCM frames
```

Async API does not require concurrent GPU inference. Do not add parallel model
execution until benchmarks show it is worth the extra VRAM, CUDA Graphs, and
static KV-cache complexity.

## Protocol Requirements

Create a versioned protocol before adding real Qwen integration.

The canonical byte-level protocol v1 specification lives in:

```text
docs/protocol-v1.md
```

Follow that document for exact frame layout, JSON fields, message directions,
error codes, request lifecycle, cancellation, backpressure, and transport rules.

The first protocol version is:

```text
QWEN_TTS_BRIDGE_PROTOCOL = 1
```

Frame header fields:

```text
magic
protocol_version
header_size
frame_type
flags
payload_size
request_id
```

`protocol_version` and `request_id` belong to the frame header only. Control and
error JSON payloads must not duplicate them.

Control payloads use:

```json
{
  "message_type": "synthesize"
}
```

Use framed messages. Never rely on one `read()` matching one logical message.

Version 1 frame types:

```text
0 reserved
1 control_json
2 audio_pcm
3 error_json
```

Use fixed-width integer fields and define byte order explicitly.

Control payloads may be UTF-8 JSON. PCM data must use binary frames. Do not
encode audio as Base64, Z85, or JSON.

Heartbeat is `ping` / `pong` over `control_json`, not a separate frame type.

Version 1 supports no non-zero flags. Do not add `final_audio_chunk`; terminal
state is represented by `completed`, `cancelled`, or `error_json`.

For WebSocket support later, one WebSocket binary message must contain exactly
one complete QTB frame. Text WebSocket messages are not allowed.

`nlohmann/json` is approved for the C++ `protocol/control` implementation.
Keep it out of public headers and do not add another JSON dependency without
explicit user approval.

## Deferred Agent Notes

Keep a visible note for intentionally deferred protocol and architecture
choices. Revisit these before making the related layer public or building a
larger abstraction on top of it:

- `protocol/control` decode functions currently assume the framing layer has
  already enforced `MAX_CONTROL_PAYLOAD_BYTES` and `MAX_ERROR_PAYLOAD_BYTES`.
  `WorkerSession` must preserve that call order. If the JSON codec becomes a
  direct public entry point or starts accepting unframed data, add an explicit
  codec-side payload-size guard.
- `error_json.category` and `error_json.code` remain strings for forward
  compatibility. The codec should require them to be present and non-empty, but
  should not enforce a closed list until the project deliberately chooses a
  stricter interoperability policy.
- `protocol/control` implementation files are intentionally split by
  responsibility, for example `ControlDecode.cpp`, `ControlEncode.cpp`,
  `ControlValidation.cpp`, `ControlJson.cpp`, and `ErrorCodec.cpp`. Keep this
  split instead of regrowing a monolithic `ControlCodec.cpp`. JSON helper
  functions should remain private to `protocol/control`; do not create a
  generic `utils` or `helpers` dumping ground unless there is real reuse across
  this subdomain.
- `src/qwen_tts_bridge/protocol/control/ControlCodecInternal.hpp` is internal
  even though the current `src/` include layout makes it technically
  includable. When the project introduces a public `include/` or install
  layout, move internal/private headers like this outside the public include
  path so downstream users cannot depend on `control_detail` or private
  implementation dependencies.
- Future native audio playback and Unity/Salsa integration notes live in
  `docs/audio-and-unity-integration.md`. Keep physical playback out of the core
  bridge; add native playback only as an optional module or example, and prefer
  one active playback sink for Unity avatar scenarios.

## Request Lifecycle

A synthesis request has these states:

```text
created
queued
running
completed
cancelled
failed
```

Allowed transitions:

```text
created -> queued
created -> running
created -> failed
queued -> running
queued -> cancelled
queued -> failed
running -> completed
running -> cancelled
running -> failed
```

Every request that reaches `queued` or `running` must produce exactly one
terminal state. Late audio chunks received after a terminal state must be
discarded and logged.

## Threading Requirements

The transport read loop must not invoke user callbacks directly.

Recommended C++ flow:

```text
application thread
    -> synthesize_async()
    -> outgoing request queue
    -> writer thread
    -> transport send

transport read callback/thread
    -> protocol parser
    -> event queue
    -> dispatcher thread
    -> user callback
```

Protect request state with explicit synchronization. Do not hold a mutex while
calling application callbacks.

Document which public methods block and which are safe to call from callbacks.
`synthesize_async()` should not wait for synthesis completion. `stop()` may
block during deterministic shutdown. `cancel()` should enqueue a cancel command
quickly and report whether the request was known locally.

Worker shutdown must be deterministic:

```text
send shutdown request
stop accepting new synthesis requests
drain or cancel active requests
wait for worker exit up to timeout
terminate as fallback
join transport and dispatcher threads
```

For cancellation to work during generation, the Python worker must not perform
blocking inference in the same loop that reads stdin. Use at least a reader
thread plus an engine thread, or an asyncio reader with blocking inference moved
to a worker thread.

## Error Model

Separate error categories:

```text
protocol_error
worker_error
model_error
request_error
timeout
resource_error
internal_error
```

Every error should include:

```text
category
code
message
request_id
```

`cancelled` is not an error category. It is a terminal control event.

`transport_error` is local to the C++ transport/supervisor layer and is not a
wire-level `error_json` category.

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
request queued
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

Use the project packaging scripts as the canonical starting point:

```text
scripts/setup-python-packaging.ps1
scripts/package-worker.ps1
scripts/test-packaged-worker.ps1
scripts/test-packaged-qwen-worker.ps1
worker/requirements-packaging.lock.txt
```

Keep packaging dependencies separate from Python development dependencies.
`package-worker.ps1 -DryRun` should remain a cheap way to validate the generated
Nuitka command without compiling. Packaging scripts default to `.venv-packaging`
when `-UseVenv` is passed so Nuitka does not pollute the development `.venv`.
Packaging scripts intentionally require Python 3.11 and default to `py -3.11`;
do not silently package with newer experimental Python versions. If a local
`.venv-packaging` was created with the wrong interpreter, remove and recreate it
instead of debugging Nuitka failures from an unsupported Python runtime.
After a real package build, run `test-packaged-worker.ps1` against the mock
backend before changing release layout. For local real-model validation, install
the vendored Qwen streaming fork into `.venv-packaging` with
`setup-python-packaging.ps1 -InstallQwenFork`, package with
`package-worker.ps1 -QwenProfile CustomVoice`, and run
`test-packaged-qwen-worker.ps1` against a local model path. The Qwen packaged
probe is manual because it depends on local model files, CUDA/PyTorch runtime
availability, and the selected model family. Full transitive packaging locks
remain later packaging work.
`-QwenProfile CustomVoice` and `-QwenProfile VoiceDesign` mean the bridge's
narrow Qwen runtime profile, not a broad `--include-package=qwen_tts`. Keep it
focused on `qwen_tts.inference`, the specific `qwen_tts.core` runtime modules
used by model/tokenizer registration, and package data. Do not pull
`qwen_tts.cli`, Gradio demo UI, development/test-only imports, non-Torch
`einops.layers` backends, the entire `qwen_tts.core` tree, or the full
Transformers model zoo into the default packaged worker graph. These profiles
should not include audio-reference dependencies such as `librosa` and
`soundfile` unless import probes show they are genuinely required. Use
`-QwenProfile VoiceClone` when voice-clone/reference audio preprocessing is in
scope, and `-QwenProfile Full` only as a diagnostic fallback.
`-IncludeQwenPackage` is a compatibility alias for
`-QwenProfile CustomVoice`.
`worker/packaging/probe_qwen_imports.py` forbids eager `librosa` and
`soundfile` imports by default. The default Qwen packaging profile also applies
`worker/packaging/nuitka-qwen-runtime.yml` to disable known compile-time bloat
entry points: Transformers' debug-only model addition context and Qwen
`librosa.filters.mel` lookups that can be replaced with an equivalent
`qwen_tts_bridge_worker.packaging` `torchaudio` mel-filter shim. Keep the shim
covered by a numerical comparison against `librosa` in packaging environments
where both libraries are installed. If SciPy or joblib reappears in
CustomVoice/VoiceDesign packaging, inspect the Nuitka report and add a narrow
package-configuration replacement instead of widening the Qwen include graph or
adding broad `--nofollow-import-to` rules.
The default Qwen packaging profile also excludes PyTorch
compile/dynamo/inductor/functorch paths because the bridge currently runs eager
inference and does not call `torch.compile` or Qwen's optional streaming
optimization setup in the packaged worker.
When investigating real Qwen packaging failures, prefer
`worker/packaging/probe_qwen_imports.py`,
`-NuitkaReportPath tmp\nuitka-worker\qwen-report.xml`, `-StrictBloatChecks`,
and targeted `-ExtraNuitkaOptions` over widening the include graph first.
The GitHub Actions workflow `Packaged Worker Smoke` is manual
(`workflow_dispatch`) by design; do not move real Nuitka compilation into every
PR check unless the build cost becomes acceptable.

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
- submit async synthesis requests without blocking the caller;
- queue multiple requests while the mock engine runs one at a time;
- fill the worker output queue with a slow client and verify memory remains
  bounded;
- simulate PCM streaming;
- simulate worker errors;
- simulate unexpected worker termination;
- verify stderr logging does not corrupt stdout protocol frames.
- verify `pong` may be delayed by backpressure but is eventually emitted.

### Phase 3: Request Lifecycle Tests

- submit multiple sequential requests without restarting the worker;
- submit multiple queued async requests;
- preserve PCM chunk order per request;
- deliver exactly one terminal event;
- discard late chunks after terminal state;
- cancel active synthesis;
- cancel queued synthesis;
- verify `completed` never overtakes the last queued PCM frame;
- verify `cancelled` does not overtake frames already queued for the request;
- verify shutdown drains terminal events and `shutdown_ack`;
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
7. Add the async `QwenTtsClient` core: request IDs, active request table,
   outgoing queue, reader/writer flow, callback dispatch, and cancellation.
8. Add a minimal console example that writes received PCM into a WAV file
   through async callbacks.
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
- `synthesize_async()` returns without waiting for inference completion;
- PCM chunks are returned incrementally;
- the chunks can be saved into a valid WAV file;
- cancellation works;
- queued requests are supported even if worker inference is sequential;
- errors are returned as structured protocol messages;
- the worker can process multiple sequential requests;
- the target machine does not require a separate Python installation;
- setup, build, packaging, and dependency steps are documented.
