# SDL_kitchensink

[![CI](https://github.com/katajakasa/SDL_kitchensink/actions/workflows/ci.yml/badge.svg)](https://github.com/katajakasa/SDL_kitchensink/actions/workflows/ci.yml)

FFmpeg and SDL2 based library for audio and video playback, written in C99.

Documentation is available at http://katajakasa.github.io/SDL_kitchensink/

Features:

* Decoding video, audio and subtitles via FFmpeg
* Dumping video and subtitle data on SDL_Textures or software surfaces
* Dumping audio data in the usual mono/stereo interleaved formats
* Automatic audio and video conversion to SDL2 friendly formats
* Synchronizing video & audio to clock
* Stream seeking
* Bitmap, text and SSA/ASS subtitle support
* Video hardware decoding (optionally)

Note! Master branch is for the development of v3.x.x series.

* v3 is under development in master branch, and is the first version to support SDL3.
* v2 can be found in the release/v2 branch. Bugfixes and new features are accepted / added. Note that v2 of
  SDL_kitchensink is the last version to support SDL2.
* v1 can be found in the release/v1 branch. Only smaller bugfixes will be accepted / added.
* v0 is no longer in development, and no fixes of any kind will be made or accepted.

| Version | SDL   | Supported          | Bugfixes           | New features       | Branch     |
|---------|-------|--------------------|--------------------|--------------------|------------|
| 3.x.x   | 3.x.x | :white_check_mark: | :white_check_mark: | :white_check_mark: | master     |
| 2.x.x   | 2.x.x | :white_check_mark: | :white_check_mark: | :white_check_mark: | release/v2 |
| 1.x.x   | 2.x.x | :white_check_mark: | :white_check_mark: | :x:                | release/v1 |
| 0.x.x   | 2.x.x | :x:                | :x:                | :x:                | release/v0 |

## 1. Library requirements

Build requirements:

* CMake (>=3.10)
* GCC (C99 support required)

Library requirements:

* SDL2 2.0.5 or newer (2.0.12 or newer recommended)
* FFmpeg 5.1 or newer
* libass (optional, supports runtime linking via SDL_LoadSO)

Note that Clang might work, but is not tested. Older SDL2 and FFmpeg library versions may or may not work; versions
noted here are the only ones tested.

### 1.1. Debian / Ubuntu

```
sudo apt-get install libsdl2-dev libavcodec-dev libavformat-dev \
    libavutil-dev libswresample-dev libswscale-dev libass-dev
```

### 1.2. MSYS2 64bit

These are for x86_64. For 32bit installation, just change the package names a bit .

```
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-libass
```

## 2. Compiling

By default, both static and dynamic libraries are built.

* Set BUILD_STATIC off if you don't want to build static library
* Set BUILD_SHARED off if you don't want to build shared library
* Dynamic library is called libSDL_kitchensink2.dll or .so
* Static library is called libSDL_kitchensink2.a
* If you build in debug mode (```-DCMAKE_BUILD_TYPE=Debug```), libraries will be postfixed with 'd'.

Change CMAKE_INSTALL_PREFIX as necessary to change the installation path. The files will be installed to

* CMAKE_INSTALL_PREFIX/lib for libraries (.dll.a, .a, etc.)
* CMAKE_INSTALL_PREFIX/bin for binaries (.dll, .so)
* CMAKE_INSTALL_PREFIX/include for headers

### 2.1. Building the libraries on Debian/Ubuntu

1. ```mkdir build && cd build```
2. ```cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..```
3. ```make -j```
4. ```sudo make install```

### 2.2. Building the libraries on MSYS2

1. ```mkdir build && cd build```
2. ```cmake -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..```
3. ```make```
4. ```make install```

### 2.3. Building examples

Just add ```-DBUILD_EXAMPLES=1``` to cmake arguments and rebuild.

### 2.4. Building with AddressSanitizer

This is for development/debugging use only!

Make sure llvm is installed, then add ```-DUSE_ASAN=1``` to the cmake arguments and rebuild. Note that ASAN is not
supported on all OSes (eg. windows).

After building, you should be able to just run the examples and get asan errors.

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
