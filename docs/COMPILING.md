# Compiling

This page covers building SDL_kitchensink from source: dependencies, a basic
release build, the full CMake option reference, and the developer builds used
for day-to-day work on the library itself.

## 1. Requirements

Build tools:

* CMake 3.10 or newer
* GCC or Clang with C99 support (GCC 13-15 and Clang 18-20 are tested in CI)
* Ninja or GNU Make

Libraries:

* SDL3 3.2.0 or newer
* FFmpeg 5.1 or newer (libavcodec, libavformat, libavutil, libswscale,
  libswresample)
* libass (optional at build time only with `USE_DYNAMIC_LIBASS`, see below)
* cmocka (only when building the test suite, `BUILD_TESTS=1`)

Older library versions may or may not work; the versions noted here are the
only ones tested.

### 1.1. Debian / Ubuntu

```sh
sudo apt-get install cmake ninja-build libsdl3-dev libavcodec-dev libavformat-dev \
    libavutil-dev libswresample-dev libswscale-dev libass-dev libcmocka-dev
```

### 1.2. Arch Linux

```sh
sudo pacman -S sdl3 ffmpeg libass cmocka cmake ninja
```

### 1.3. MSYS2 64bit

These are for x86_64. For a 32bit installation, adjust the package name
prefixes accordingly.

```sh
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-SDL3 mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-libass \
    mingw-w64-x86_64-cmocka
```

## 2. Building the library

By default both a shared and a static library are built:

* The shared library is called `libSDL_kitchensink3.so` (or `.dll`)
* The static library is called `libSDL_kitchensink3.a`
* Debug builds (`-DCMAKE_BUILD_TYPE=Debug`) postfix the names with `d`

### 2.1. Linux

```sh
cmake -GNinja -S. -Bbuild -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja -C build
sudo ninja -C build install
```

GNU Make also works: drop `-GNinja` and use `make -j` / `sudo make install`
inside the build directory instead.

### 2.2. MSYS2

```sh
cmake -GNinja -S. -Bbuild -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja -C build
ninja -C build install
```

### 2.3. Installation layout

Files are installed under `CMAKE_INSTALL_PREFIX` following GNUInstallDirs:

* Headers to `include/kitchensink3/`
* Shared and static libraries to `lib/` (runtime DLLs to `bin/` on Windows)
* A pkg-config file `SDL_kitchensink3.pc` to `lib/pkgconfig/`

## 3. CMake options

| Option                | Default | Description                                                                                       |
|-----------------------|---------|---------------------------------------------------------------------------------------------------|
| `BUILD_SHARED`        | ON      | Build the shared library.                                                                         |
| `BUILD_STATIC`        | ON      | Build the static library. At least one of the two must be on.                                     |
| `BUILD_EXAMPLES`      | OFF     | Build the example programs (see below).                                                           |
| `BUILD_TESTS`         | OFF     | Build the test suite. Requires `BUILD_STATIC=ON` and cmocka. See [TESTING.md](TESTING.md).        |
| `USE_ASAN`            | OFF     | Build with AddressSanitizer. Mutually exclusive with `USE_TSAN`. Development use only.            |
| `USE_TSAN`            | OFF     | Build with ThreadSanitizer. Mutually exclusive with `USE_ASAN`. Development use only.             |
| `USE_TIDY`            | OFF     | Run clang-tidy on library sources during the build. See [STYLE_GUIDE.md](STYLE_GUIDE.md).         |
| `USE_FORMAT`          | OFF     | Add a `clangformat` target that reformats all sources. See [STYLE_GUIDE.md](STYLE_GUIDE.md).      |
| `KIT_FAULT_INJECTION` | OFF     | Compile in the debug fault-injection registry, used only by the test suite.                       |
| `USE_DYNAMIC_LIBASS`  | OFF     | Load libass at runtime with `SDL_LoadSO()` instead of linking it. Not recommended; the hardcoded library name may need patching for your platform. |

Sanitizer notes: ASan/TSan builds are for development and debugging only, and
are not supported on all platforms (e.g. Windows). See
[TESTING.md](TESTING.md) for running the test suite under sanitizers.

## 4. Building the examples

Add `-DBUILD_EXAMPLES=1` to the CMake arguments. This builds the example
programs from the `examples/` directory: `simple`, `complex`, `audio`,
`custom`, `rwops` and `rawdump`, plus `glcube` if an OpenGL development
library is found.

The examples are not meant for real-life use; they only demonstrate simple use
cases for the library.

## 5. Developer builds

The top-level `Makefile` wraps the CMake invocations used for day-to-day
development. It uses Ninja.

| Target            | What it does                                                                                             |
|-------------------|----------------------------------------------------------------------------------------------------------|
| `make release`    | Release build with examples and tests, installed into `./build/release`.                                 |
| `make build-asan` | Debug build with AddressSanitizer, fault injection, clang-format and clang-tidy enabled.                 |
| `make build-tsan` | Same as `build-asan` but with ThreadSanitizer instead.                                                   |
| `make test-asan`  | `build-asan` + runs the test suite with the LeakSanitizer suppressions applied.                          |
| `make test-tsan`  | `build-tsan` + runs the test suite with the ThreadSanitizer suppressions applied.                        |
| `make clean`      | Removes the `build/` directory.                                                                          |

Before opening a pull request, `make test-asan` and `make test-tsan` should
both pass; CI runs the same configurations. See
[CONTRIBUTING.md](../CONTRIBUTING.md).

Once a build is configured with `USE_FORMAT=1` (the sanitizer targets above
do this), `ninja -C build clangformat` reformats all sources in place. See
[STYLE_GUIDE.md](STYLE_GUIDE.md).

## 6. API documentation

`make docs` inside the build directory (or `doxygen` in the repository root)
generates the Doxygen API documentation. The published documentation at
<http://katajakasa.github.io/SDL_kitchensink/> is rebuilt automatically from
the master, release/v2 and release/v1 branches on every push.
