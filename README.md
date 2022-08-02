# SDL_kitchensink

[![CI](https://github.com/katajakasa/SDL_kitchensink/actions/workflows/ci.yml/badge.svg)](https://github.com/katajakasa/SDL_kitchensink/actions/workflows/ci.yml)

FFmpeg and SDL2 based library for audio and video playback, written in C99.

Documentation is available at http://katajakasa.github.io/SDL_kitchensink/

Features:
* Decoding video, audio and subtitles via FFmpeg
* Dumping video and subtitle data on SDL_textures
* Dumping audio data in the usual mono/stereo interleaved formats
* Automatic audio and video conversion to SDL2 friendly formats
* Synchronizing video & audio to clock
* Seeking forwards and backwards
* Bitmap, text and SSA/ASS subtitle support

Note! Master branch is for the development of v1.0.0 series. v0 can be found in the 
rel-kitchensink-0 branch. v0 is no longer in active development and only bug- and security-fixes
are accepted.

## 1. Installation

Nowadays you can find SDL_kitchensink in eg. linux repositories. Installation might be as simple as
running the following (or your distributions' equivalent):

```apt install libsdl-kitchensink libsdl-kitchensink-dev```

If you are running on windows/MSYS2 or on linux distributions where the package management does not
have kitchensink, you will need to compile it yourself. Please see the "Compiling" section below.

## 2. Library requirements

Build requirements:
* CMake (>=3.7)
* GCC (C99 support required)

Library requirements:
* SDL2 2.0.5 or newer
* FFmpeg 3.2 or newer
* libass (optional, supports runtime linking via SDL_LoadSO)

Note that Clang might work, but is not tested. Older SDL2 and FFmpeg library versions
may or may not work; versions noted here are the only ones tested.

### 2.1. Debian / Ubuntu

```
sudo apt-get install libsdl2-dev libavcodec-dev libavformat-dev \
    libavutil-dev libswresample-dev libswscale-dev libass-dev
```

### 2.2. MSYS2 64bit

These are for x86_64. For 32bit installation, just change the package names a bit .
```
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-libass
```

## 3. Compiling

By default, both static and dynamic libraries are built.
* Set BUILD_STATIC off if you don't want to build static library
* Set BUILD_SHARED off if you don't want to build shared library
* Dynamic library is called libSDL_kitchensink.dll or .so
* Static library is called libSDL_kitchensink_static.a
* If you build in debug mode (```-DCMAKE_BUILD_TYPE=Debug```), libraries will be postfixed with 'd'.

Change CMAKE_INSTALL_PREFIX as necessary to change the installation path. The files will be installed to
* CMAKE_INSTALL_PREFIX/lib for libraries (.dll.a, .a, etc.)
* CMAKE_INSTALL_PREFIX/bin for binaries (.dll, .so)
* CMAKE_INSTALL_PREFIX/include for headers

### 3.1. Building the libraries on Debian/Ubuntu

1. ```mkdir build && cd build```
2. ```cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..```
3. ```make -j```
4. ```sudo make install```

### 3.2. Building the libraries on MSYS2

1. ```mkdir build && cd build```
2. ```cmake -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..```
3. ```make```
4. ```make install```

### 3.3. Building examples

Just add ```-DBUILD_EXAMPLES=1``` to cmake arguments and rebuild.

### 3.4. Building with AddressSanitizer

This is for development/debugging use only!

Make sure llvm is installed, then add ```-DUSE_ASAN=1``` to the cmake arguments and rebuild. Note that ASAN is not
supported on all OSes (eg. windows).

After building, you can run with the following (make sure to set correct llvm-symbolizer path):
```
ASAN_OPTIONS=symbolize=1 ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer ./complex <my videofile>
```

## 4. Q&A

Q: What's with the USE_DYNAMIC_LIBASS cmake flag ?
* A: It can be used to link the libass dynamically when needed. This also makes it possible to build the
     library without libass, if needed. Using this flag is not recommended however, and it will probably
     be deprecated in the next major version(s). If you use it, you might need to also patch the library
     path and name to match yours in kitchensink source.

Q: Why the name SDL_kitchensink
* A: Because pulling major blob of library code like ffmpeg feels like bringing in a whole house with its
     kitchensink and everything to the project. Also, it sounded funny. Also, SDL_ffmpeg is already reserved :(

## 5. Examples

Please see examples directory. You can also take a look at unittests for some help.
Note that examples are NOT meant for any kind of real life use; they are only meant to
show simple use cases for the library.

## 6. FFMPEG & licensing

Note that FFmpeg has a rather complex license. Please take a look at 
[FFmpeg Legal page](http://ffmpeg.org/legal.html) for details.

## 7. License

```
The MIT License (MIT)

Copyright (c) 2020 Tuomas Virtanen

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
