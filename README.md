# SDL_kitchensink

FFMPEG and SDL2 based library for audio and video playback.

## 1. Library requirements

Build requirements:
* CMake (>=2.8)
* GCC (C99 support required)

Clang might work, but is not tested!

Library requirements:
* SDL2 (>=2.0.3)
* FFMPEG (>=2.8)
* CUnit (optional, for unittests)

Older package versions may or may not work; versions noted here are the only ones tested.

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

### 2.1. Building the libraries

1. ```cmake -G "MSYS Makefiles" -DCMAKE_BULD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..```
2. ```make```
3. ```sudo make install```
4. Profit!

### 2.2. Building examples

Just add ```-DBUILD_EXAMPLES=1``` to cmake arguments and rebuild.

### 2.3. Building unittests

Make sure CUnit is installed, then add ```-DBUILD_UNITTESTS=1``` to the cmake arguments and rebuild.

## 3. License

MIT. Please see ```LICENSE``` for details.
