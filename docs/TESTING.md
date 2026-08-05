# Testing

The test suite is built on [cmocka](https://cmocka.org/) and driven by CTest.
It is enabled with the `BUILD_TESTS=1` CMake option, which requires
`BUILD_STATIC=ON` (tests link the static library so they can reach internal
symbols).

## 1. Test tiers

Tests live under `tests/`, organized into three tiers plus shared helpers:

* **`tests/unit`** -- isolated tests for single internal components: packet
  buffer, timer, texture atlas, audio/video utils, decoder plumbing, and so
  on. The `*_mt` variants exercise the same components from multiple threads.
* **`tests/api`** -- tests for the public API surface: library lifecycle,
  error handling, sources and custom I/O, formats, utils.
* **`tests/decoder`** -- integration tests that decode real media fixtures:
  audio/video/texture format matrices, playback bounds, seeking, stream
  switching, subtitle rendering, broken input, stress tests, and the
  fault-injection sweeps.
* **`tests/common`** -- shared test helpers (assertion, lifecycle, playback
  and fault-sweep harnesses, an in-memory source) and the sanitizer
  suppression files `lsan.supp` and `tsan.supp`.

Each test file `tests/<tier>/test_<name>.c` becomes an executable
`test_<name>` and a CTest entry `<tier>/<name>`, labeled with its tier. Tests
run with SDL's dummy video/audio drivers, so no display or sound hardware is
needed; they also have a 120-second timeout each.

## 2. Running the tests

The most common way is through the top-level Makefile, which configures,
builds and runs everything under a sanitizer:

```sh
make test-asan    # build + run tests under AddressSanitizer
make test-tsan    # build + run tests under ThreadSanitizer
```

You can also be more selective:

```sh
ninja -C build check                          # build test executables, then run ctest
ctest --test-dir build -L unit                # only the unit tier (also: api, decoder)
ctest --test-dir build -LE stress             # everything except stress tests
ctest --test-dir build -R packetbuffer        # tests matching a name
ctest --test-dir build --output-on-failure    # show test output for failures
```

## 3. Fault injection

Configuring with `-DKIT_FAULT_INJECTION=1` compiles a debug-only
fault-injection registry into the library. Library code checks named *fail
points* (for example `alloc`, `demux_read`,
`demux_seek`, `decode_send`, `decode_receive`, `sdl_mutex`, `swr_init`,
`sws_init`) at fallible spots, and a test can arm a fail point so a window of
future calls through it fails deterministically.

This enables the tests that verify error-path behavior: constructor unwind
sweeps (`test_alloc_unwind`), demuxer I/O failures (`test_fault_io`), decoder
errors (`test_fault_decode`) and resource-creation failures
(`test_fault_resources`). These tests are only registered when the option is
on. The option is enabled in the `build-asan`/`build-tsan` Makefile targets
and in the CI sanitizer runs, so the error paths get exercised under a
sanitizer; it should never be enabled in a production build.

## 4. Test media

The decoder-tier tests read media fixtures from `test-data/media/`. The
fixtures are generated files, but they are committed to git -- the main build
and CI never regenerate them. This keeps test inputs byte-identical
everywhere without requiring every environment to carry a full set of ffmpeg
encoders.

To add or change fixtures, edit `test-data/Makefile` (fixture sources live in
`test-data/src/`) and regenerate:

```sh
make -C test-data
git add test-data/media/...   # commit the files you meant to change
```

Regeneration needs the ffmpeg CLI with the libx264, libx265, libvpx-vp9, aac,
libmp3lame, libvorbis and libopus encoders, python3 (for the VobSub
generator), and a DejaVu Sans or Liberation Sans TTF font (for the
font-attachment fixture; override with `FONT=/path/to/font.ttf`). The
Makefile checks these up front and fails with a clear message if something is
missing.

Encodes run with bitexact flags so regeneration is byte-stable on the same
ffmpeg build. An ffmpeg version bump can still change output bytes even when
the content is semantically unchanged, so avoid committing wholesale
regenerations unless that is what you intend.
