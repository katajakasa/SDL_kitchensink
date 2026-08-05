# Architecture

This page describes how the repository is laid out and how the library works
internally. It is aimed at people working on SDL_kitchensink itself; for
using the library, see the [API documentation](http://katajakasa.github.io/SDL_kitchensink/)
and the `examples/` directory.

## 1. Repository layout

```
include/kitchensink3/           Public API headers (installed)
include/kitchensink3/internal/  Internal headers (not installed)
src/                            Public API implementation
src/internal/                   Demuxer, decoders, threads, buffers
src/internal/audio/             Audio decoder + resampling
src/internal/video/             Video decoder + scaling/conversion
src/internal/subtitle/          Subtitle decoder, atlas, renderers
src/internal/utils/             Small shared helpers
examples/                       Example programs (BUILD_EXAMPLES=1)
tests/                          Test suite, see docs/TESTING.md
test-data/                      Committed test media + its generator
cmake/                          CMake find modules and helpers
docs/                           This developer documentation
```

## 2. Public API vs internals

Everything under `include/kitchensink3/*.h` is public, stable API:

* `kitchensink.h` -- umbrella header that includes all of the below
* `kitlib.h` -- library lifecycle
* `kitsource.h` -- sources
* `kitplayer.h` -- the player
* `kiterror.h` -- errors
* `kitformat.h` -- formats
* `kitcodec.h` -- codec info
* `kitutils.h` -- utils
* `kitconfig.h` -- export/visibility macros (`KIT_API`, `KIT_LOCAL`)

These headers are installed, documented with Doxygen, and changing them
incompatibly is a major-version event.

Everything under `include/kitchensink3/internal/` and `src/internal/` is
private. Internal symbols are marked `KIT_LOCAL` (hidden visibility in shared
builds, see `kitconfig.h`) and may change freely between releases. Tests link
the static library, so they can and do test internal components directly.

Errors are reported through `Kit_SetError()` and read with `Kit_GetError()`;
the error message storage is thread-local (SDL TLS), so each thread sees only
its own errors.

## 3. The playback pipeline

A `Kit_Player` runs a pipeline of one demuxer thread and up to three decoder
threads (video, audio, subtitle -- one per selected stream). The application's
own thread is the final consumer, pulling output through the `Kit_GetPlayer*`
functions.

```mermaid
flowchart LR
    subgraph app["Application thread"]
        SRC["Kit_Source<br/>(URL / SDL_IOStream / custom I/O)"]
        OUT["Kit_GetPlayerVideoSDLTexture()<br/>Kit_GetPlayerAudioData()<br/>Kit_GetPlayerSubtitleSDLTexture()"]
    end
    subgraph demux["Demuxer thread"]
        DEM["Kit_Demuxer<br/>av_read_frame()"]
    end
    subgraph decoders["Decoder threads"]
        VDEC["Video decoder"]
        ADEC["Audio decoder"]
        SDEC["Subtitle decoder"]
    end
    SRC --> DEM
    DEM -->|"packet buffer"| VDEC
    DEM -->|"packet buffer"| ADEC
    DEM -->|"packet buffer"| SDEC
    VDEC -->|"frame buffer"| OUT
    ADEC -->|"sample FIFO"| OUT
    SDEC -->|"texture atlas /<br/>frame buffer"| OUT
```

Stage by stage:

* **`Kit_Source`** wraps an FFmpeg `AVFormatContext`. It can be created from
  a URL, an `SDL_IOStream`, or custom read/seek callbacks.
* **`Kit_Demuxer`**, driven by its **`Kit_DemuxerThread`**, reads packets
  from the source and routes each one into a per-stream-type
  **`Kit_PacketBuffer`**. Packets for unselected streams are dropped.
  Transient read errors are retried a configurable number of times; on EOF
  the thread writes an EOF-tagged sentinel packet into every active buffer so
  the decoders know to drain.
* **`Kit_Decoder`** is a generic wrapper that owns the `AVCodecContext` and
  handles hardware-decoder negotiation. The type-specific behavior (decode,
  flush, abort, output buffering) is plugged in through callbacks by the
  audio, video and subtitle decoders. Each decoder is driven by its own
  **`Kit_DecoderThread`**, which pulls packets from the demuxer's packet
  buffer, feeds them to the codec, and pushes decoded output into the
  decoder's output buffer.
* **Output** happens on the application's thread: video frames are
  synchronized against the playback clock and uploaded to an SDL texture (or
  locked for raw access), audio is read out as interleaved samples sized for
  the audio backend's buffer, and subtitles are rendered onto a texture
  atlas or returned as raw frames.

### 3.1. Packet buffers

`Kit_PacketBuffer` is the mechanism that decouples the threads: a
thread-safe, fixed-capacity ring buffer of
pre-allocated slots, with the slot type abstracted behind
alloc/unref/free/move/ref callbacks (the same structure carries `AVPacket`s
between demuxer and decoders, and decoded frames on the output side).

Writes block while the buffer is full and reads can block (with a timeout)
while it is empty -- this provides natural backpressure: when the application
stops consuming, decoder output buffers fill, decoder threads stall on their
writes, the demuxer's packet buffers fill, and the demuxer thread stalls
too. `Kit_AbortPacketBuffer()` wakes all waiters and makes their calls fail,
which is how shutdown avoids deadlocking on blocked threads; a subsequent
flush clears the aborted state.

### 3.2. Clock and seeking

Playback is synchronized against a shared, reference-counted timer. The
player owns the primary timer; decoders hold
secondary handles onto the same underlying clock value. One stream is the
primary sync source -- video when present, otherwise audio -- meaning its
decoder is allowed to (re)base the shared clock; output is then shown,
skipped or delayed relative to the clock within the configurable early/late
thresholds of `Kit_PlayerConfig`.

Seeks are handled by the demuxer: `Kit_PlayerSeek()` stops the pipeline
threads, queues the seek target on the demuxer thread, and restarts it. The
demuxer seeks the format context and, on success, flushes all packet buffers,
bumps the timer's *serial*, and writes a seek-tagged packet carrying the new
serial into every active buffer. Decoder threads see the marker, discard
stale output, and re-base the clock -- this is how frames decoded before the
seek are told apart from frames decoded after it.

### 3.3. Thread lifecycle

Thread stop is a two-step protocol, visible throughout the internal APIs:
clearing a thread's run flag only asks it to exit at its next loop check,
while a thread blocked on a full/empty packet buffer must additionally be
woken through the abort call of its buffer/decoder/demuxer, or the join would
deadlock. The `Kit_Close*` functions bundle stop + abort + join in the right
order.

## 4. Subtitles

Subtitle rendering sits behind a small renderer abstraction with two
implementations: text-based formats (SRT, SSA/ASS) are rendered through
libass, while bitmap subtitle formats (e.g. DVD/VobSub) have their own
renderer. Rendered bitmaps are packed into a texture atlas so a frame's
worth of subtitle fragments can be drawn from a single texture.

libass itself is used either as a normal link-time dependency, or -- with the
`USE_DYNAMIC_LIBASS` CMake option -- loaded at runtime via `SDL_LoadSO()`,
in which case an internal header mirrors the needed libass API as function
pointers. The library-wide singleton `Kit_LibraryState` holds the init flags
and libass handles; all per-player tuning lives in `Kit_PlayerConfig`.

## 5. Fault injection

With the `KIT_FAULT_INJECTION` CMake option, a debug-only fault-injection
registry is compiled in. Fallible spots in the
library check named fail points through it, letting tests deterministically
fail allocations, demuxer I/O, decoder calls and resource creation to
exercise error paths. It is for the test suite only and must not be enabled
in production builds. See [TESTING.md](TESTING.md).
