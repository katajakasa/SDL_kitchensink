# SDL_kitchensink

[![CI](https://github.com/katajakasa/SDL_kitchensink/actions/workflows/ci.yml/badge.svg)](https://github.com/katajakasa/SDL_kitchensink/actions/workflows/ci.yml)

FFmpeg and SDL3 based library for audio and video playback, written in C99.

Documentation is available at http://katajakasa.github.io/SDL_kitchensink/dev/

Features:

* Decoding video, audio and subtitles via FFmpeg
* Dumping video and subtitle data on SDL_Textures or software surfaces
* Dumping audio data in the usual mono/stereo interleaved formats
* Automatic audio and video conversion to SDL3 friendly formats
* Synchronizing video & audio to clock
* Stream seeking
* Bitmap, text and SSA/ASS subtitle support
* Video hardware decoding (optionally)

Note! Master branch is for the development of the v3.x.x series, the first to
support SDL3. The v2 series (the last to support SDL2) and older series live
in release branches; see [CONTRIBUTING.md](CONTRIBUTING.md) for the branch
and support table.

## 1. Quickstart

On Debian/Ubuntu:

```sh
sudo apt-get install cmake ninja-build libsdl3-dev libavcodec-dev libavformat-dev \
    libavutil-dev libswresample-dev libswscale-dev libass-dev
cmake -GNinja -S. -Bbuild -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja -C build
sudo ninja -C build install
```

For other platforms (Arch Linux, MSYS2), CMake options, examples and
sanitizer builds, see [docs/COMPILING.md](docs/COMPILING.md).

## 2. Development

See [CONTRIBUTING.md](CONTRIBUTING.md) for how development is organized, and
the developer docs for details:

* [docs/COMPILING.md](docs/COMPILING.md) -- dependencies, building, CMake options
* [docs/TESTING.md](docs/TESTING.md) -- test suite, sanitizers, test media
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) -- project structure and internals
* [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md) -- code style and conventions

## 3. Q&A

Q: What's with the USE_DYNAMIC_LIBASS cmake flag ?

* A: It can be used to link the libass dynamically when needed. This also makes it possible to build the library without
  libass, if needed. Using this flag is not recommended however, and it will probably be deprecated in the next major
  version (s). If you use it, you might need to also patch the library path and name to match yours in kitchensink
  source.

Q: Why the name SDL_kitchensink

* A: Because pulling major blob of library code like ffmpeg feels like bringing in a whole house with its kitchensink
  and everything to the project. Also, it sounded funny. Also, SDL_ffmpeg is already reserved :(

## 4. Examples

Please see examples directory. You can also take a look at unittests for some help. Note that examples are NOT meant for
any kind of real life use; they are only meant to show simple use cases for the library.

## 5. FFMPEG & licensing

Note that FFmpeg has a rather complex license. Please take a look at
[FFmpeg Legal page](http://ffmpeg.org/legal.html) for details.

## 6. License

```
The MIT License (MIT)

Copyright (c) Tuomas Virtanen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
