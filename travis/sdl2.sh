
if [ ! -e "$HOME/local/lib/libSDL2.so" ]; then
    wget https://www.libsdl.org/release/SDL2-2.0.5.tar.gz -O ~/SDL2.tar.gz
    tar -xzvf ~/SDL2.tar.gz -C ~/
    mkdir ~/sdl-build
    cd ~/sdl-build
    export CC=gcc-5
    ~/SDL2-2.0.5/configure --prefix=$HOME/local
    make -j2
    make install
else 
    echo 'Using cached SDL2 build directory.';
fi
