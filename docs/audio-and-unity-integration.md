# Audio Playback and Unity Integration Notes

This document captures design notes for future audio playback helpers and Unity
integration. It is not part of the first implementation scope.

## Core Responsibility

The core `qwen_tts_bridge` library should not own physical audio playback.

The core bridge is responsible for:

```text
start and supervise the worker
send synthesis requests
receive streaming PCM chunks
deliver PCM to the application through callbacks or queues
```

The application decides what to do with the PCM:

```text
play audio
save WAV
send over local IPC or network
feed a virtual microphone
run DSP or analysis
drive an avatar integration
```

Keeping playback outside the core avoids turning the bridge into an audio-device
framework with device selection, device loss, underruns, volume, pause/resume,
and platform-specific playback behavior.

## Optional Native Playback Module

A native playback helper can be added later as an optional module or example,
not as a required part of `qwen_tts_bridge`.

Possible layout:

```text
examples/miniaudio_player/
    local playback smoke test

optional module:
    qwen_tts_bridge_audio
```

Possible class shape:

```cpp
StreamingAudioPlayer player;

callbacks.on_started = [&](const AudioFormat& format) {
    player.open(format);
};

callbacks.on_audio = [&](const PcmChunk& chunk) {
    player.enqueue(chunk);
};

callbacks.on_completed = [&] {
    player.finish();
};
```

The playback helper must use a bounded PCM queue. It must not accumulate
unbounded audio when the device cannot consume data quickly enough.

For the first expected format:

```text
s16le
mono
24000 Hz
```

The byte rate is:

```text
24000 samples/sec * 2 bytes = 48000 bytes/sec
```

A 0.5-2 second playback buffer is usually enough for a local test player:

```text
0.5 sec  ~= 24 KiB
1.0 sec  ~= 48 KiB
2.0 sec  ~= 96 KiB
```

## Preferred Playback Backend

If a native playback helper is added, prefer evaluating `miniaudio` first.

Reasons:

- it matches a simple pull-based audio callback model;
- it can read from a bounded ring buffer;
- it supports common platform backends, including WASAPI on Windows;
- it can perform format, sample-rate, and channel conversion;
- it is small enough for an optional example or helper module.

Expected flow:

```text
WorkerSession callback
    -> bounded PCM ring buffer
    -> miniaudio data callback
    -> audio device
```

The audio callback must stay realtime-friendly:

- do not wait for the worker;
- do not block on long mutex operations;
- do not allocate heavily;
- do not start, stop, or destroy the audio device from inside the callback;
- fill underruns with silence instead of blocking.

Pseudo-code:

```cpp
void audio_callback(
    ma_device* device,
    void* output,
    const void*,
    ma_uint32 frame_count)
{
    auto* player = static_cast<AudioPlayer*>(device->pUserData);

    const std::size_t requested_bytes =
        frame_count * player->channels() * sizeof(std::int16_t);

    const std::size_t read =
        player->ring_buffer().read(output, requested_bytes);

    if (read < requested_bytes) {
        std::memset(
            static_cast<std::byte*>(output) + read,
            0,
            requested_bytes - read);
    }
}
```

OpenAL Soft is still a valid option when the host application already uses
OpenAL, or when positioned/spatial 3D speech becomes important. For ordinary
mono TTS playback, it adds sources, listener state, context management, and
buffer recycling that the bridge does not otherwise need.

SDL3 Audio is a good choice when the host application already uses SDL3.
PortAudio is useful for professional low-level I/O, microphone capture, ASIO,
or very low-latency scenarios. Neither should be introduced only for basic TTS
playback without a specific reason.

Adding any playback dependency must follow the project dependency policy:
ask first, add it as a pinned git submodule, and keep it out of the core bridge
unless the project explicitly changes scope.

## Unity And Lip-Sync

When Unity owns the avatar and lip-sync, Unity should receive the audio stream.
Think of this as fan-out of one TTS stream to consumers, not as duplicating
physical playback.

Preferred production path for an avatar:

```text
Qwen3 Python worker
    -> raw PCM
qwen3-tts-bridge-cpp / WorkerSession
    -> raw PCM over local IPC, WebSocket, TCP, or named pipe
Unity integration
    -> bounded PCM ring buffer
    -> streaming AudioClip / AudioSource
        -> Salsa lip-sync
        -> OBS / speakers
```

The C++ bridge remains the only owner of the Python worker and session state:

```text
Python worker
    <-> C++ bridge
    <-> Unity client
```

Do not connect Unity directly to the worker stdout when C++ is the supervisor.
That creates multiple owners of worker/session state.

## One Active Playback Sink

Avoid sending the same audio to two physical playback paths at once.

Bad production setup:

```text
C++ miniaudio/OpenAL playback
+
Unity AudioSource playback
```

This can produce echo, comb filtering, or visible lip-sync drift because two
players may start with different latency. Even 50-150 ms can be noticeable.

Prefer one active sink:

```text
Unity sink
```

or:

```text
native C++ sink
```

PCM can still be fanned out for recording, analysis, streaming, or diagnostics.
Only physical playback should be single-owner.

## Unity Audio Flow

Unity can play streaming PCM through a streaming `AudioClip`:

```csharp
AudioClip.Create(
    "TTS",
    bufferLengthSamples,
    channels,
    sampleRate,
    true,
    OnAudioRead);
```

`OnAudioRead(float[] data)` pulls the next samples from a Unity-side ring
buffer. Network or IPC callbacks should only enqueue PCM bytes; they should not
touch most Unity objects directly.

Threading shape:

```text
network / IPC callback
    -> bounded PCM ring buffer
    -> Unity audio callback
    -> AudioSource
    -> Salsa
```

The bridge currently expects raw `s16le` PCM. Unity audio data uses floats, so
the Unity adapter must convert:

```text
int16 PCM -> float [-1.0, +1.0]
```

Example:

```csharp
float sample = shortSample / 32768.0f;
```

If Salsa is configured to analyze an `AudioSource`, it can use the same audio
that the audience hears. Separate viseme commands are only worth considering if
the TTS engine later exposes reliable phoneme or viseme timestamps.

## Unity-Facing Transport

The existing QTB framing model is already binary and can carry raw PCM without
Base64. A Unity adapter can reuse QTB concepts or define a small Unity-facing
protocol with the same event shape:

```text
started:
    request_id
    sample_format = s16le
    sample_rate = 24000
    channels = 1

audio_pcm:
    request_id
    raw PCM bytes

completed / cancelled / error:
    request_id
    terminal state or diagnostics
```

Potential local transports:

```text
C++ -> WebSocket -> Unity
C++ -> TCP -> Unity
C++ -> named pipe -> Unity
```

WebSocket is attractive if the Unity side benefits from existing libraries and
debug tooling. Named pipes are a good Windows-local option, but they may require
more Unity-side plumbing.

## Recommended Future Layout

Keep the core independent from playback and Unity:

```text
src/qwen_tts_bridge/
    core protocol, transport, session, and public API

adapters/unity/
    UnityAudioStreamServer or Unity-facing transport adapter

examples/miniaudio_player/
    optional native playback smoke test

examples/write_wav/
    save streamed PCM to a WAV file
```

The first usable version should still prioritize:

- WorkerSession;
- request lifecycle;
- cancellation;
- PCM callbacks;
- WAV output example.

Native playback and Unity integration should come after the core streaming path
is stable.

## References

- miniaudio: https://miniaud.io/docs/manual/index.html
- OpenAL Soft: https://github.com/kcat/openal-soft
- SDL3 AudioStream: https://wiki.libsdl.org/SDL3/SDL_AudioStream
- PortAudio: https://www.portaudio.com/docs/v19-doxydocs/start_stop_abort.html
