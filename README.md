# SDL_kitchensink

FFMPEG and SDL2 based C99 library for audio and video playback.

This library is still very much todo, but it's slowly getting there.

Can do:
* Decoding video & audio
* Dumping video data on SDL_textures
* Dumping audio data in the usual mono/stereo interleaved formats
* Automatic audio and video conversion to SDL2 friendly formats
* Synchronizing video & audio to computer clock
* Seeking

Won't do (probably):
* Encoding

Should be done better:
* Synchronizing. It is currently pretty crude (though works).
* Buffer size tuning. Current ones are decent guesses.
* Seeking currently only seeks keyframes, and may or may not work properly with more exotic streams.
* Documentation (!!!)

## 1. Library requirements

Build requirements:
* CMake (>=2.8)
* GCC (C99 support required)

Library requirements:
* SDL2 (>=2.0.3) (Note! Examples require 2.0.4!)
* FFMPEG (>=2.8)
* CUnit (optional, for unittests)

Note that Clang might work, but is not tested. Older SDL2 and FFMPEG library versions
may or may not work; versions noted here are the only ones tested.

### 1.1. Debian / Ubuntu

```
sudo apt-get install libsdl2-dev libavcodec-dev libavdevice-dev libavfilter-dev \
libavformat-dev libavresample-dev libavutil-dev libswresample-dev libswscale-dev \
libpostproc-dev
```

### 1.2. MSYS2 64bit

These are for x86_64. For 32bit installation, just change the package names a bit .
```
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-ffmpeg

```

## 2. Compiling

By default, both static and dynamic libraries are built.
* Dynamic library is called libSDL_kitchensink.dll or .so
* Static library is called libSDL_kitchensink_static.a
* If you build in debug mode (```-DCMAKE_BUILD_TYPE=Debug```), libraries will be postfixed with 'd'.

Change CMAKE_INSTALL_PREFIX as necessary to change the installation path. The files will be installed to
* CMAKE_INSTALL_PREFIX/lib for libraries (.dll.a, .a, etc.)
* CMAKE_INSTALL_PREFIX/bin for binaries (.dll, .so)
* CMAKE_INSTALL_PREFIX/include for headers

### 2.1. Building the libraries on Debian/Ubuntu

1. ```mkdir build && cd build```
2. ```cmake -DCMAKE_BULD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..```
3. ```make -j```
4. ```sudo make install```

### 2.2. Building the libraries on MSYS2

1. ```mkdir build && cd build```
2. ```cmake -G "MSYS Makefiles" -DCMAKE_BULD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..```
3. ```make```
4. ```make install```

### 2.3. Building examples

Just add ```-DBUILD_EXAMPLES=1``` to cmake arguments and rebuild.

### 2.4. Building unittests

Make sure CUnit is installed, then add ```-DBUILD_UNITTESTS=1``` to the cmake arguments and rebuild.

You can run unittests by running ```make unittest```.

## 3. License

MIT. Please see ```LICENSE``` for details.

Note that FFmpeg has a rather complex license. Please take a look at [FFmpeg Legal page](http://ffmpeg.org/legal.html)
for details.

## 4. FAQ

### Q: Why SDL_kitchensink ?
A: Because pulling major blob of library code like ffmpeg feels like bringing in a whole house with its
   kitchensink and everything to the project. Also, it sounded funny. Also, SDL_ffmpeg is already reserved :(

### Q: Documentation ?
A: None yet. Maybe more doxygen later.

### Q: Encoding ?
A: Not going to happen. Decoding and playback only.

## 5. Examples

Please see examples directory. You can also take a look at unittests for some help.
Note that examples are NOT meant for any kind of real life use; they are only meant to
show simple use cases for the library.
